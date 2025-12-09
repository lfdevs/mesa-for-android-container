/*
 * Copyright 1998-1999 Precision Insight, Inc., Cedar Park, Texas.
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
 *   Kevin E. Martin <kevin@precisioninsight.com>
 *   Brian Paul <brian@precisioninsight.com>
 *   Kristian Høgsberg (krh@redhat.com)
 */

#if defined(GLX_DIRECT_RENDERING) && (!defined(GLX_USE_APPLEGL) || defined(GLX_USE_APPLE))

#include <unistd.h>
#include <dlfcn.h>
#include <stdarg.h>
#include "glxclient.h"
#include "dri_common.h"
#include "loader.h"
#include <X11/Xlib-xcb.h>
#include <xcb/xproto.h>
#include "dri_util.h"
#include "pipe-loader/pipe_loader.h"
#include "x11/x11_display.h"

#define __ATTRIB(attrib, field) \
    { attrib, offsetof(struct glx_config, field) }

static const struct
{
   unsigned int attrib, offset;
} attribMap[] = {
   __ATTRIB(__DRI_ATTRIB_BUFFER_SIZE, rgbBits),
      __ATTRIB(__DRI_ATTRIB_LEVEL, level),
      __ATTRIB(__DRI_ATTRIB_RED_SIZE, redBits),
      __ATTRIB(__DRI_ATTRIB_GREEN_SIZE, greenBits),
      __ATTRIB(__DRI_ATTRIB_BLUE_SIZE, blueBits),
      __ATTRIB(__DRI_ATTRIB_ALPHA_SIZE, alphaBits),
      __ATTRIB(__DRI_ATTRIB_DEPTH_SIZE, depthBits),
      __ATTRIB(__DRI_ATTRIB_STENCIL_SIZE, stencilBits),
      __ATTRIB(__DRI_ATTRIB_ACCUM_RED_SIZE, accumRedBits),
      __ATTRIB(__DRI_ATTRIB_ACCUM_GREEN_SIZE, accumGreenBits),
      __ATTRIB(__DRI_ATTRIB_ACCUM_BLUE_SIZE, accumBlueBits),
      __ATTRIB(__DRI_ATTRIB_ACCUM_ALPHA_SIZE, accumAlphaBits),
      __ATTRIB(__DRI_ATTRIB_SAMPLE_BUFFERS, sampleBuffers),
      __ATTRIB(__DRI_ATTRIB_SAMPLES, samples),
      __ATTRIB(__DRI_ATTRIB_DOUBLE_BUFFER, doubleBufferMode),
      __ATTRIB(__DRI_ATTRIB_STEREO, stereoMode),
      __ATTRIB(__DRI_ATTRIB_AUX_BUFFERS, numAuxBuffers),
      __ATTRIB(__DRI_ATTRIB_BIND_TO_TEXTURE_RGB, bindToTextureRgb),
      __ATTRIB(__DRI_ATTRIB_BIND_TO_TEXTURE_RGBA, bindToTextureRgba),
      __ATTRIB(__DRI_ATTRIB_BIND_TO_MIPMAP_TEXTURE, bindToMipmapTexture),
      __ATTRIB(__DRI_ATTRIB_YINVERTED, yInverted),
      __ATTRIB(__DRI_ATTRIB_FRAMEBUFFER_SRGB_CAPABLE, sRGBCapable)
};

static int
scalarEqual(struct glx_config *mode, unsigned int attrib, unsigned int value)
{
   unsigned glxValue, i;

   for (i = 0; i < ARRAY_SIZE(attribMap); i++)
      if (attribMap[i].attrib == attrib) {
         glxValue = *(unsigned int *) ((char *) mode + attribMap[i].offset);
         return glxValue == GLX_DONT_CARE || glxValue == value;
      }

   return GL_TRUE;              /* Is a non-existing attribute equal to value? */
}

static int
driConfigEqual(struct glx_config *config, const struct dri_config *driConfig)
{
   unsigned int attrib, value, glxValue;
   int i;

   i = 0;
   while (driIndexConfigAttrib(driConfig, i++, &attrib, &value)) {
      switch (attrib) {
      case __DRI_ATTRIB_RENDER_TYPE:
         glxValue = 0;
         if (value & __DRI_ATTRIB_RGBA_BIT) {
            glxValue |= GLX_RGBA_BIT;
         }
         if (value & __DRI_ATTRIB_COLOR_INDEX_BIT) {
            glxValue |= GLX_COLOR_INDEX_BIT;
         }
         if (value & __DRI_ATTRIB_FLOAT_BIT) {
            glxValue |= GLX_RGBA_FLOAT_BIT_ARB;
         }
         if (value & __DRI_ATTRIB_UNSIGNED_FLOAT_BIT) {
            glxValue |= GLX_RGBA_UNSIGNED_FLOAT_BIT_EXT;
         }
         if (glxValue != config->renderType)
            return GL_FALSE;
         break;

      case __DRI_ATTRIB_BIND_TO_TEXTURE_TARGETS:
         glxValue = 0;
         if (value & __DRI_ATTRIB_TEXTURE_1D_BIT)
            glxValue |= GLX_TEXTURE_1D_BIT_EXT;
         if (value & __DRI_ATTRIB_TEXTURE_2D_BIT)
            glxValue |= GLX_TEXTURE_2D_BIT_EXT;
         if (value & __DRI_ATTRIB_TEXTURE_RECTANGLE_BIT)
            glxValue |= GLX_TEXTURE_RECTANGLE_BIT_EXT;
         if (config->bindToTextureTargets != GLX_DONT_CARE &&
             glxValue != config->bindToTextureTargets)
            return GL_FALSE;
         break;

      /* Nerf some attributes we can safely ignore if the server claims to
       * support them but the driver does not.
       */
      case __DRI_ATTRIB_CONFIG_CAVEAT:
         if (value & __DRI_ATTRIB_NON_CONFORMANT_CONFIG)
            glxValue = GLX_NON_CONFORMANT_CONFIG;
         else if (value & __DRI_ATTRIB_SLOW_BIT)
            glxValue = GLX_SLOW_CONFIG;
         else
            glxValue = GLX_NONE;
         if (glxValue != config->visualRating) {
            if (config->visualRating == GLX_NONE) {
               static int warned;
               if (!warned) {
                  DebugMessageF("Not downgrading visual rating\n");
                  warned = 1;
               }
            } else {
               return GL_FALSE;
            }
         }
         break;

      case __DRI_ATTRIB_AUX_BUFFERS:
         if (!scalarEqual(config, attrib, value)) {
            static int warned;
            if (!warned) {
               DebugMessageF("Disabling server's aux buffer support\n");
               warned = 1;
            }
            config->numAuxBuffers = 0;
         }
         break;

      case __DRI_ATTRIB_BIND_TO_MIPMAP_TEXTURE:
         if (!scalarEqual(config, attrib, value)) {
            static int warned;
            if (!warned) {
               DebugMessageF("Disabling server's tfp mipmap support\n");
               warned = 1;
            }
            config->bindToMipmapTexture = 0;
         }
         break;

      default:
         if (!scalarEqual(config, attrib, value))
            return GL_FALSE;
      }
   }

   return GL_TRUE;
}

static struct glx_config *
createDriMode(struct glx_config *config, const struct dri_config **driConfigs)
{
   __GLXDRIconfigPrivate *driConfig;
   int i;

   for (i = 0; driConfigs[i]; i++) {
      if (driConfigEqual(config, driConfigs[i]))
         break;
   }

   if (driConfigs[i] == NULL)
      return NULL;

   driConfig = malloc(sizeof *driConfig);
   if (driConfig == NULL)
      return NULL;

   driConfig->base = *config;
   driConfig->driConfig = driConfigs[i];

   return &driConfig->base;
}

struct glx_config *
driConvertConfigs(struct glx_config *configs, const struct dri_config **driConfigs)
{
   struct glx_config head, *tail, *m;

   tail = &head;
   head.next = NULL;
   for (m = configs; m; m = m->next) {
      tail->next = createDriMode(m, driConfigs);
      if (tail->next == NULL) {
         /* no matching dri config for m */
         continue;
      }


      tail = tail->next;
   }

   return head.next;
}

void
driDestroyConfigs(const struct dri_config **configs)
{
   int i;

   for (i = 0; configs[i]; i++)
      free((struct dri_config *) configs[i]);
   free(configs);
}

static struct glx_config *
driInferDrawableConfig(struct glx_screen *psc, GLXDrawable draw)
{
   unsigned int fbconfig = 0;
   xcb_get_window_attributes_cookie_t cookie = { 0 };
   xcb_get_window_attributes_reply_t *attr = NULL;
   xcb_connection_t *conn = XGetXCBConnection(psc->dpy);

   /* In practice here, either the XID is a bare Window or it was created
    * by some other client. First let's see if the X server can tell us
    * the answer. Xorg first added GLX_EXT_no_config_context in 1.20, where
    * this usually works except for bare Windows that haven't been made
    * current yet.
    */
   if (__glXGetDrawableAttribute(psc->dpy, draw, GLX_FBCONFIG_ID, &fbconfig)) {
      return glx_config_find_fbconfig(psc->configs, fbconfig);
   }

   /* Well this had better be a Window then. Figure out its visual and
    * then find the corresponding GLX visual.
    */
   cookie = xcb_get_window_attributes(conn, draw);
   attr = xcb_get_window_attributes_reply(conn, cookie, NULL);

   if (attr) {
      uint32_t vid = attr->visual;
      free(attr);
      return glx_config_find_visual(psc->visuals, vid);
   }

   return NULL;
}

__GLXDRIdrawable *
driFetchDrawable(struct glx_context *gc, GLXDrawable glxDrawable)
{
   Display *dpy = gc->psc->dpy;
   struct glx_display *const priv = __glXInitialize(dpy);
   __GLXDRIdrawable *pdraw;
   struct glx_screen *psc = gc->psc;
   struct glx_config *config = gc->config;
   unsigned int type;

   if (priv == NULL)
      return NULL;

   if (glxDrawable == None)
      return NULL;

   if (priv->drawHash == NULL)
      return NULL;

   if (__glxHashLookup(priv->drawHash, glxDrawable, (void *) &pdraw) == 0) {
      /* Resurrected, so remove from the alive-query-set if exist. */
      _mesa_set_remove_key(priv->zombieGLXDrawable, pdraw);

      pdraw->refcount ++;
      return pdraw;
   }

   /* if this is a no-config context, infer the fbconfig from the drawable */
   if (config == NULL)
      config = driInferDrawableConfig(psc, glxDrawable);
   if (config == NULL)
      return NULL;

   /* We can't find this GLX drawable above because it's either:
    *
    * 1. An X window ID instead of a GLX window ID. This could happend when
    *    glXMakeCurrent() is passed an X window directly instead of creating
    *    GLXWindow with glXCreateWindow() first.
    *
    * 2. A GLXPbuffer created on other display:
    *
    *    From the GLX spec:
    *
    *      Like other drawable types, GLXPbuffers are shared; any client which
    *      knows the associated XID can use a GLXPbuffer.
    *
    *    So client other than the creator of this GLXPbuffer could use its
    *    XID to do something like glXMakeCurrent(). I can't find explicite
    *    statement in GLX spec that also allow GLXWindow and GLXPixmap.
    *
    *    But even GLXWindow and GLXPixmap is allowed, currently client other
    *    than the GLX drawable creator has no way to find which X drawable
    *    (window or pixmap) this GLX drawable uses, except the GLXPbuffer
    *    case which use the same XID for both X pixmap and GLX drawable.
    */

   /* Infer the GLX drawable type. */
   if (__glXGetDrawableAttribute(dpy, glxDrawable, GLX_DRAWABLE_TYPE, &type)) {
      /* Xserver may support query with raw X11 window. */
      if (type == GLX_PIXMAP_BIT) {
         ErrorMessageF("GLXPixmap drawable type is not supported\n");
         return NULL;
      }
   } else {
      /* Xserver may not implement GLX_DRAWABLE_TYPE query yet. */
      type = GLX_PBUFFER_BIT | GLX_WINDOW_BIT;
   }

   pdraw = psc->driScreen.createDrawable(psc, glxDrawable, glxDrawable,
                                          type, config);

   if (pdraw == NULL) {
      ErrorMessageF("failed to create drawable\n");
      return NULL;
   }

   if (__glxHashInsert(priv->drawHash, glxDrawable, pdraw)) {
      pdraw->destroyDrawable(pdraw);
      return NULL;
   }
   pdraw->refcount = 1;

   return pdraw;
}

static int
discardGLXBadDrawableHandler(Display *display, xError *err, XExtCodes *codes,
                             int *ret_code)
{
   int code = codes->first_error + GLXBadDrawable;

   /* Only discard error which is expected. */
   if (err->majorCode == codes->major_opcode &&
       err->minorCode == X_GLXGetDrawableAttributes &&
       /* newer xserver use GLXBadDrawable, old one use BadDrawable */
       (err->errorCode == code || err->errorCode == BadDrawable)) {
      *ret_code = 1;
      return 1;
   }

   return 0;
}

static void
checkServerGLXDrawableAlive(const struct glx_display *priv)
{
   ErrorType old = XESetError(priv->dpy, priv->codes.extension,
                              discardGLXBadDrawableHandler);

   set_foreach(priv->zombieGLXDrawable, entry) {
      __GLXDRIdrawable *pdraw = (__GLXDRIdrawable *)entry->key;
      GLXDrawable drawable = pdraw->drawable;
      unsigned int dummy;

      /* Fail to query, so the window has been closed. Release the GLXDrawable. */
      if (!__glXGetDrawableAttribute(priv->dpy, drawable, GLX_WIDTH, &dummy)) {
         pdraw->destroyDrawable(pdraw);
         __glxHashDelete(priv->drawHash, drawable);
         _mesa_set_remove(priv->zombieGLXDrawable, entry);
      }
   }

   XESetError(priv->dpy, priv->codes.extension, old);
}

static void
releaseDrawable(const struct glx_display *priv, GLXDrawable drawable)
{
   __GLXDRIdrawable *pdraw;

   if (__glxHashLookup(priv->drawHash, drawable, (void *) &pdraw) == 0) {
      /* Only native window and pbuffer have same GLX and X11 drawable ID. */
      if (pdraw->drawable == pdraw->xDrawable) {
         pdraw->refcount --;
         /* If pbuffer's refcount reaches 0, it must be imported from other
          * display. Because pbuffer created from this display will always
          * hold the last refcount until destroy the GLXPbuffer object.
          */
         if (pdraw->refcount == 0) {
            if (pdraw->psc->keep_native_window_glx_drawable) {
               checkServerGLXDrawableAlive(priv);
               _mesa_set_add(priv->zombieGLXDrawable, pdraw);
            } else {
               pdraw->destroyDrawable(pdraw);
               __glxHashDelete(priv->drawHash, drawable);
            }
         }
      }
   }
}

void
driReleaseDrawables(struct glx_context *gc)
{
   const struct glx_display *priv = gc->psc->display;

   releaseDrawable(priv, gc->currentDrawable);
   releaseDrawable(priv, gc->currentReadable);

   gc->currentDrawable = None;
   gc->currentReadable = None;

}

int
dri_convert_glx_attribs(unsigned num_attribs, const uint32_t *attribs,
                        struct dri_ctx_attribs *dca)
{
   unsigned i;
   uint32_t profile = GLX_CONTEXT_CORE_PROFILE_BIT_ARB;

   dca->major_ver = 1;
   dca->minor_ver = 0;
   dca->render_type = GLX_RGBA_TYPE;
   dca->reset = __DRI_CTX_RESET_NO_NOTIFICATION;
   dca->release = __DRI_CTX_RELEASE_BEHAVIOR_FLUSH;
   dca->flags = 0;
   dca->api = __DRI_API_OPENGL;
   dca->no_error = 0;

   for (i = 0; i < num_attribs; i++) {
      switch (attribs[i * 2]) {
      case GLX_CONTEXT_MAJOR_VERSION_ARB:
    dca->major_ver = attribs[i * 2 + 1];
    break;
      case GLX_CONTEXT_MINOR_VERSION_ARB:
    dca->minor_ver = attribs[i * 2 + 1];
    break;
      case GLX_CONTEXT_FLAGS_ARB:
    dca->flags = attribs[i * 2 + 1];
    break;
      case GLX_CONTEXT_OPENGL_NO_ERROR_ARB:
    dca->no_error = attribs[i * 2 + 1];
    break;
      case GLX_CONTEXT_PROFILE_MASK_ARB:
    profile = attribs[i * 2 + 1];
    break;
      case GLX_RENDER_TYPE:
         dca->render_type = attribs[i * 2 + 1];
    break;
      case GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB:
         switch (attribs[i * 2 + 1]) {
         case GLX_NO_RESET_NOTIFICATION_ARB:
            dca->reset = __DRI_CTX_RESET_NO_NOTIFICATION;
            break;
         case GLX_LOSE_CONTEXT_ON_RESET_ARB:
            dca->reset = __DRI_CTX_RESET_LOSE_CONTEXT;
            break;
         default:
            return BadMatch;
         }
         break;
      case GLX_CONTEXT_RELEASE_BEHAVIOR_ARB:
         switch (attribs[i * 2 + 1]) {
         case GLX_CONTEXT_RELEASE_BEHAVIOR_NONE_ARB:
            dca->release = __DRI_CTX_RELEASE_BEHAVIOR_NONE;
            break;
         case GLX_CONTEXT_RELEASE_BEHAVIOR_FLUSH_ARB:
            dca->release = __DRI_CTX_RELEASE_BEHAVIOR_FLUSH;
            break;
         default:
            return BadValue;
         }
         break;
      case GLX_SCREEN:
         /* Implies GLX_EXT_no_config_context */
         dca->render_type = GLX_DONT_CARE;
         break;
      default:
    /* If an unknown attribute is received, fail.
     */
    return BadValue;
      }
   }

   switch (profile) {
   case GLX_CONTEXT_CORE_PROFILE_BIT_ARB:
      /* This is the default value, but there are no profiles before OpenGL
       * 3.2. The GLX_ARB_create_context_profile spec says:
       *
       *     "If the requested OpenGL version is less than 3.2,
       *     GLX_CONTEXT_PROFILE_MASK_ARB is ignored and the functionality
       *     of the context is determined solely by the requested version."
       */
      dca->api = (dca->major_ver > 3 || (dca->major_ver == 3 && dca->minor_ver >= 2))
         ? __DRI_API_OPENGL_CORE : __DRI_API_OPENGL;
      break;
   case GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB:
      dca->api = __DRI_API_OPENGL;
      break;
   case GLX_CONTEXT_ES_PROFILE_BIT_EXT:
      if (dca->major_ver == 3  && dca->minor_ver <= 2)
         dca->api = __DRI_API_GLES3;
      else if (dca->major_ver == 2 && dca->minor_ver == 0)
         dca->api = __DRI_API_GLES2;
      else if (dca->major_ver == 1 && dca->minor_ver < 2)
         dca->api = __DRI_API_GLES;
      else {
         return GLXBadProfileARB;
      }
      break;
   default:
      return GLXBadProfileARB;
   }

   /* Unknown flag value */
   if (dca->flags & ~(__DRI_CTX_FLAG_DEBUG |
                      __DRI_CTX_FLAG_FORWARD_COMPATIBLE |
                      __DRI_CTX_FLAG_ROBUST_BUFFER_ACCESS |
                      __DRI_CTX_FLAG_RESET_ISOLATION))
      return BadValue;

   /* There are no forward-compatible contexts before OpenGL 3.0.  The
    * GLX_ARB_create_context spec says:
    *
    *     "Forward-compatible contexts are defined only for OpenGL versions
    *     3.0 and later."
    */
   if (dca->major_ver < 3 && (dca->flags & __DRI_CTX_FLAG_FORWARD_COMPATIBLE) != 0)
      return BadMatch;

   /* It also says:
    *
    *    "OpenGL contexts supporting version 3.0 or later of the API do not
    *    support color index rendering, even if a color index <config> is
    *    available."
    */
   if (dca->major_ver >= 3 && dca->render_type == GLX_COLOR_INDEX_TYPE)
      return BadMatch;

   /* The KHR_no_error specs say:
    *
    *    Requires OpenGL ES 2.0 or OpenGL 2.0.
    */
   if (dca->no_error && dca->major_ver < 2)
      return BadMatch;

   /* The GLX_ARB_create_context_no_error specs say:
    *
    *    BadMatch is generated if the GLX_CONTEXT_OPENGL_NO_ERROR_ARB is TRUE at
    *    the same time as a debug or robustness context is specified.
    *
    */
   if (dca->no_error && ((dca->flags & __DRI_CTX_FLAG_DEBUG) ||
                         (dca->flags & __DRI_CTX_FLAG_ROBUST_BUFFER_ACCESS)))
      return BadMatch;

   return Success;
}

unsigned
dri_context_error_to_glx_error(unsigned error)
{
   if (error == __DRI_CTX_ERROR_SUCCESS)
      return Success;
   if (error == __DRI_CTX_ERROR_NO_MEMORY)
      return BadAlloc;
   else if (error == __DRI_CTX_ERROR_BAD_API)
      return BadMatch;
   else if (error == __DRI_CTX_ERROR_BAD_VERSION)
      return GLXBadFBConfig;
   else if (error == __DRI_CTX_ERROR_BAD_FLAG)
      return BadMatch;
   else if (error == __DRI_CTX_ERROR_UNKNOWN_ATTRIBUTE)
      return BadValue;
   else if (error == __DRI_CTX_ERROR_UNKNOWN_FLAG)
      return BadValue;
   else
      UNREACHABLE("Impossible DRI context error");
}

struct glx_context *
dri_common_create_context(struct glx_screen *base,
                          struct glx_config *config_base,
                          struct glx_context *shareList,
                          int renderType)
{
   unsigned int error;
   uint32_t attribs[2] = { GLX_RENDER_TYPE, renderType };

   return base->vtable->create_context_attribs(base, config_base, shareList,
                                               1, attribs, &error);
}


/*
 * Given a display pointer and screen number, determine the name of
 * the DRI driver for the screen (i.e., "i965", "radeon", "nouveau", etc).
 * Return True for success, False for failure.
 */
static Bool
driGetDriverName(Display * dpy, int scrNum, char **driverName)
{
   struct glx_screen *glx_screen = GetGLXScreenConfigs(dpy, scrNum);

   if (!glx_screen || !glx_screen->vtable->get_driver_name)
      return False;

   *driverName = glx_screen->vtable->get_driver_name(glx_screen);
   return True;
}

/*
 * Exported function for querying the DRI driver for a given screen.
 *
 * The returned char pointer points to a static array that will be
 * overwritten by subsequent calls.
 */
_GLX_PUBLIC const char *
glXGetScreenDriver(Display * dpy, int scrNum)
{
   static char ret[32];
   char *driverName;

   if (driGetDriverName(dpy, scrNum, &driverName)) {
      int len;
      if (!driverName)
         return NULL;
      len = strlen(driverName);
      if (len >= 31)
         return NULL;
      memcpy(ret, driverName, len + 1);
      free(driverName);
      return ret;
   }
   return NULL;
}

/* glXGetDriverConfig must return a pointer with a static lifetime. To avoid
 * keeping drivers loaded and other leaks, we keep a cache of results here that
 * is cleared by an atexit handler.
 */
struct driver_config_entry {
   struct driver_config_entry *next;
   char *driverName;
   char *config;
};

static pthread_mutex_t driver_config_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct driver_config_entry *driver_config_cache = NULL;

/* Called as an atexit function. Otherwise, this would have to be called with
 * driver_config_mutex locked.
 */
static void
clear_driver_config_cache()
{
   while (driver_config_cache) {
      struct driver_config_entry *e = driver_config_cache;
      driver_config_cache = e->next;

      free(e->driverName);
      free(e->config);
      free(e);
   }
}

/*
 * Exported function for obtaining a driver's option list (UTF-8 encoded XML).
 *
 * The returned char pointer points directly into the driver. Therefore
 * it should be treated as a constant.
 *
 * If the driver was not found or does not support configuration NULL is
 * returned.
 */
_GLX_PUBLIC const char *
glXGetDriverConfig(const char *driverName)
{
   struct driver_config_entry *e;

   pthread_mutex_lock(&driver_config_mutex);

   for (e = driver_config_cache; e; e = e->next) {
      if (strcmp(e->driverName, driverName) == 0)
         goto out;
   }

   e = malloc(sizeof(*e));
   if (!e)
      goto out;

   e->config = driGetDriInfoXML(driverName);
   e->driverName = strdup(driverName);
   if (!e->config || !e->driverName) {
      free(e->config);
      free(e->driverName);
      free(e);
      e = NULL;
      goto out;
   }

   e->next = driver_config_cache;
   driver_config_cache = e;

   if (!e->next)
      atexit(clear_driver_config_cache);

out:
   pthread_mutex_unlock(&driver_config_mutex);

   return e ? e->config : NULL;
}

Bool
dri_bind_context(struct glx_context *context, GLXDrawable draw, GLXDrawable read)
{
   __GLXDRIdrawable *pdraw, *pread;
   struct dri_drawable *dri_draw = NULL, *dri_read = NULL;

   pdraw = driFetchDrawable(context, draw);
   pread = driFetchDrawable(context, read);

   driReleaseDrawables(context);

   if (pdraw)
      dri_draw = pdraw->dri_drawable;
   else if (draw != None)
      return GLXBadDrawable;

   if (pread)
      dri_read = pread->dri_drawable;
   else if (read != None)
      return GLXBadDrawable;

   if (!driBindContext(context->driContext, dri_draw, dri_read))
      return GLXBadContext;

   if (context->psc->display->driver == GLX_DRIVER_DRI3 ||
       context->psc->display->driver == GLX_DRIVER_ZINK_YES) {
      if (dri_draw)
         dri_invalidate_drawable(dri_draw);
      if (dri_read && dri_read != dri_draw)
         dri_invalidate_drawable(dri_read);
   }

   return Success;
}

void
dri_unbind_context(struct glx_context *context)
{
   driUnbindContext(context->driContext);
}

void
dri_destroy_context(struct glx_context *context)
{
   driReleaseDrawables(context);
 
   free((char *) context->extensions);
 
   driDestroyContext(context->driContext);
 
   free(context);
}

struct glx_context *
dri_create_context_attribs(struct glx_screen *base,
                           struct glx_config *config_base,
                           struct glx_context *shareList,
                           unsigned num_attribs,
                           const uint32_t *attribs,
                           unsigned *error)
{
   struct glx_context *pcp = NULL;
   __GLXDRIconfigPrivate *config = (__GLXDRIconfigPrivate *) config_base;
   struct dri_context *shared = NULL;

   struct dri_ctx_attribs dca;
   uint32_t ctx_attribs[2 * 6];
   unsigned num_ctx_attribs = 0;

   *error = dri_convert_glx_attribs(num_attribs, attribs, &dca);
   if (*error != __DRI_CTX_ERROR_SUCCESS)
      goto error_exit;

   /* Check the renderType value */
   if (!validate_renderType_against_config(config_base, dca.render_type)) {
      *error = BadValue;
      goto error_exit;
   }

   if (shareList) {
      /* We can't share with an indirect context */
      if (!shareList->isDirect)
         return NULL;

      /* The GLX_ARB_create_context_no_error specs say:
       *
       *    BadMatch is generated if the value of GLX_CONTEXT_OPENGL_NO_ERROR_ARB
       *    used to create <share_context> does not match the value of
       *    GLX_CONTEXT_OPENGL_NO_ERROR_ARB for the context being created.
       */
      if (!!shareList->noError != !!dca.no_error) {
         *error = BadMatch;
         return NULL;
      }

      shared = shareList->driContext;
   }

   pcp = calloc(1, sizeof *pcp);
   if (pcp == NULL) {
      *error = BadAlloc;
      goto error_exit;
   }

   if (!glx_context_init(pcp, base, config_base))
      goto error_exit;

   ctx_attribs[num_ctx_attribs++] = __DRI_CTX_ATTRIB_MAJOR_VERSION;
   ctx_attribs[num_ctx_attribs++] = dca.major_ver;
   ctx_attribs[num_ctx_attribs++] = __DRI_CTX_ATTRIB_MINOR_VERSION;
   ctx_attribs[num_ctx_attribs++] = dca.minor_ver;

   /* Only send a value when the non-default value is requested.  By doing
    * this we don't have to check the driver's DRI3 version before sending the
    * default value.
    */
   if (dca.reset != __DRI_CTX_RESET_NO_NOTIFICATION) {
      ctx_attribs[num_ctx_attribs++] = __DRI_CTX_ATTRIB_RESET_STRATEGY;
      ctx_attribs[num_ctx_attribs++] = dca.reset;
   }

   if (dca.release != __DRI_CTX_RELEASE_BEHAVIOR_FLUSH) {
      ctx_attribs[num_ctx_attribs++] = __DRI_CTX_ATTRIB_RELEASE_BEHAVIOR;
      ctx_attribs[num_ctx_attribs++] = dca.release;
   }

   if (dca.no_error) {
      ctx_attribs[num_ctx_attribs++] = __DRI_CTX_ATTRIB_NO_ERROR;
      ctx_attribs[num_ctx_attribs++] = dca.no_error;
      pcp->noError = GL_TRUE;
   }

   if (dca.flags != 0) {
      ctx_attribs[num_ctx_attribs++] = __DRI_CTX_ATTRIB_FLAGS;
      ctx_attribs[num_ctx_attribs++] = dca.flags;
   }

   /* The renderType is retrieved from attribs, or set to default
    *  of GLX_RGBA_TYPE.
    */
   pcp->renderType = dca.render_type;

   pcp->driContext =
      driCreateContextAttribs(base->frontend_screen,
                              dca.api,
                              config ? config->driConfig : NULL,
                              shared,
                              num_ctx_attribs / 2,
                              ctx_attribs,
                              error,
                              pcp,
                              x11_xlib_display_is_thread_safe(base->dpy));

   *error = dri_context_error_to_glx_error(*error);

   if (pcp->driContext == NULL)
      goto error_exit;

   pcp->vtable = base->context_vtable;

   return pcp;

error_exit:
   free(pcp);

   return NULL;
}

char *
dri_get_driver_name(struct glx_screen *glx_screen)
{
    return strdup(glx_screen->driverName);
}

const struct glx_screen_vtable dri_screen_vtable = {
   .create_context         = dri_common_create_context,
   .create_context_attribs = dri_create_context_attribs,
   .query_renderer_integer = glx_dri_query_renderer_integer,
   .query_renderer_string  = glx_dri_query_renderer_string,
   .get_driver_name        = dri_get_driver_name,
};

void
dri_bind_tex_image(__GLXDRIdrawable *base, int buffer, const int *attrib_list)
{
   struct glx_context *gc = __glXGetCurrentContext();

   if (!base)
      return;

   if (base->psc->display->driver == GLX_DRIVER_DRI3) {
      dri_invalidate_drawable(base->dri_drawable);

      XSync(gc->currentDpy, false);
   }

   dri_set_tex_buffer2(gc->driContext,
                        base->textureTarget,
                        base->textureFormat,
                        base->dri_drawable);
}

bool
dri_screen_init(struct glx_screen *psc, struct glx_display *priv, int screen, int fd, const __DRIextension **loader_extensions, bool driver_name_is_inferred)
{
   const struct dri_config **driver_configs;
   struct glx_config *configs = NULL, *visuals = NULL;

   if (!glx_screen_init(psc, screen, priv))
      return false;

   enum dri_screen_type type;
   switch (psc->display->driver) {
   case GLX_DRIVER_DRI3:
      type = DRI_SCREEN_DRI3;
      break;
   case GLX_DRIVER_ZINK_YES:
      type = DRI_SCREEN_KOPPER;
      break;
   case GLX_DRIVER_SW:
      type = DRI_SCREEN_SWRAST;
      break;
   default:
      UNREACHABLE("unknown glx driver type");
   }

   psc->frontend_screen = driCreateNewScreen3(screen, fd,
                                                 loader_extensions,
                                                 type,
                                                 &driver_configs, driver_name_is_inferred,
                                                 psc->display->has_multibuffer, psc);

   if (psc->frontend_screen == NULL) {
      goto handle_error;
   }

   configs = driConvertConfigs(psc->configs, driver_configs);
   visuals = driConvertConfigs(psc->visuals, driver_configs);

   if (!configs || !visuals) {
       ErrorMessageF("No matching fbConfigs or visuals found\n");
       goto handle_error;
   }

   glx_config_destroy_list(psc->configs);
   psc->configs = configs;
   glx_config_destroy_list(psc->visuals);
   psc->visuals = visuals;

   psc->driver_configs = driver_configs;

   psc->vtable = &dri_screen_vtable;
   psc->driScreen.bindTexImage = dri_bind_tex_image;

   return true;

handle_error:
   if (configs)
       glx_config_destroy_list(configs);
   if (visuals)
       glx_config_destroy_list(visuals);

   return false;
}
#endif /* GLX_DIRECT_RENDERING */
