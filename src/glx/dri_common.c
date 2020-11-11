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

#if defined(GLX_DIRECT_RENDERING) && !defined(GLX_USE_APPLEGL)

#include <unistd.h>
#include <dlfcn.h>
#include <stdarg.h>
#include "glxclient.h"
#include "dri_common.h"
#include "loader.h"

#ifndef RTLD_NOW
#define RTLD_NOW 0
#endif
#ifndef RTLD_GLOBAL
#define RTLD_GLOBAL 0
#endif

_X_HIDDEN void
dri_message(int level, const char *f, ...)
{
   va_list args;
   int threshold = _LOADER_WARNING;
   const char *libgl_debug;

   libgl_debug = getenv("LIBGL_DEBUG");
   if (libgl_debug) {
      if (strstr(libgl_debug, "quiet"))
         threshold = _LOADER_FATAL;
      else if (strstr(libgl_debug, "verbose"))
         threshold = _LOADER_DEBUG;
   }

   /* Note that the _LOADER_* levels are lower numbers for more severe. */
   if (level <= threshold) {
      fprintf(stderr, "libGL%s: ", level <= _LOADER_WARNING ? " error" : "");
      va_start(args, f);
      vfprintf(stderr, f, args);
      va_end(args);
   }
}

#ifndef GL_LIB_NAME
#define GL_LIB_NAME "libGL.so.1"
#endif

/**
 * Try to \c dlopen the named driver.
 *
 * This function adds the "_dri.so" suffix to the driver name and searches the
 * directories specified by the \c LIBGL_DRIVERS_PATH environment variable in
 * order to find the driver.
 *
 * \param driverName - a name like "i965", "radeon", "nouveau", etc.
 * \param out_driver_handle - Address to return the resulting dlopen() handle.
 *
 * \returns
 * The __DRIextension entrypoint table for the driver, or \c NULL if driver
 * file not found.
 */
_X_HIDDEN const __DRIextension **
driOpenDriver(const char *driverName, void **out_driver_handle)
{
   void *glhandle;

   /* Attempt to make sure libGL symbols will be visible to the driver */
   glhandle = dlopen(GL_LIB_NAME, RTLD_NOW | RTLD_GLOBAL);

   static const char *search_path_vars[] = {
      "LIBGL_DRIVERS_PATH",
      "LIBGL_DRIVERS_DIR", /* deprecated */
      NULL
   };

   const __DRIextension **extensions =
      loader_open_driver(driverName, out_driver_handle, search_path_vars);

   if (glhandle)
      dlclose(glhandle);

   return extensions;
}

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
#if 0
      __ATTRIB(__DRI_ATTRIB_TRANSPARENT_TYPE, transparentPixel),
      __ATTRIB(__DRI_ATTRIB_TRANSPARENT_INDEX_VALUE, transparentIndex),
      __ATTRIB(__DRI_ATTRIB_TRANSPARENT_RED_VALUE, transparentRed),
      __ATTRIB(__DRI_ATTRIB_TRANSPARENT_GREEN_VALUE, transparentGreen),
      __ATTRIB(__DRI_ATTRIB_TRANSPARENT_BLUE_VALUE, transparentBlue),
      __ATTRIB(__DRI_ATTRIB_TRANSPARENT_ALPHA_VALUE, transparentAlpha),
      __ATTRIB(__DRI_ATTRIB_RED_MASK, redMask),
      __ATTRIB(__DRI_ATTRIB_GREEN_MASK, greenMask),
      __ATTRIB(__DRI_ATTRIB_BLUE_MASK, blueMask),
      __ATTRIB(__DRI_ATTRIB_ALPHA_MASK, alphaMask),
      __ATTRIB(__DRI_ATTRIB_RED_SHIFT, redShift),
      __ATTRIB(__DRI_ATTRIB_GREEN_SHIFT, greenShift),
      __ATTRIB(__DRI_ATTRIB_BLUE_SHIFT, blueShift),
      __ATTRIB(__DRI_ATTRIB_ALPHA_SHIFT, alphaShift),
#endif
      __ATTRIB(__DRI_ATTRIB_MAX_PBUFFER_WIDTH, maxPbufferWidth),
      __ATTRIB(__DRI_ATTRIB_MAX_PBUFFER_HEIGHT, maxPbufferHeight),
      __ATTRIB(__DRI_ATTRIB_MAX_PBUFFER_PIXELS, maxPbufferPixels),
      __ATTRIB(__DRI_ATTRIB_OPTIMAL_PBUFFER_WIDTH, optimalPbufferWidth),
      __ATTRIB(__DRI_ATTRIB_OPTIMAL_PBUFFER_HEIGHT, optimalPbufferHeight),
      __ATTRIB(__DRI_ATTRIB_SWAP_METHOD, swapMethod),
      __ATTRIB(__DRI_ATTRIB_BIND_TO_TEXTURE_RGB, bindToTextureRgb),
      __ATTRIB(__DRI_ATTRIB_BIND_TO_TEXTURE_RGBA, bindToTextureRgba),
      __ATTRIB(__DRI_ATTRIB_BIND_TO_MIPMAP_TEXTURE,
                     bindToMipmapTexture),
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
driConfigEqual(const __DRIcoreExtension *core,
               struct glx_config *config, const __DRIconfig *driConfig)
{
   unsigned int attrib, value, glxValue;
   int i;

   i = 0;
   while (core->indexConfigAttrib(driConfig, i++, &attrib, &value)) {
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

      case __DRI_ATTRIB_CONFIG_CAVEAT:
         if (value & __DRI_ATTRIB_NON_CONFORMANT_CONFIG)
            glxValue = GLX_NON_CONFORMANT_CONFIG;
         else if (value & __DRI_ATTRIB_SLOW_BIT)
            glxValue = GLX_SLOW_CONFIG;
         else
            glxValue = GLX_NONE;
         if (glxValue != config->visualRating)
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

      case __DRI_ATTRIB_SWAP_METHOD:
         if (value == __DRI_ATTRIB_SWAP_EXCHANGE)
            glxValue = GLX_SWAP_EXCHANGE_OML;
         else if (value == __DRI_ATTRIB_SWAP_COPY)
            glxValue = GLX_SWAP_COPY_OML;
         else
            glxValue = GLX_SWAP_UNDEFINED_OML;

         if (!scalarEqual(config, attrib, glxValue))
            return GL_FALSE;

         break;

      default:
         if (!scalarEqual(config, attrib, value))
            return GL_FALSE;
      }
   }

   return GL_TRUE;
}

static struct glx_config *
createDriMode(const __DRIcoreExtension * core,
	      struct glx_config *config, const __DRIconfig **driConfigs)
{
   __GLXDRIconfigPrivate *driConfig;
   int i;

   for (i = 0; driConfigs[i]; i++) {
      if (driConfigEqual(core, config, driConfigs[i]))
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

_X_HIDDEN struct glx_config *
driConvertConfigs(const __DRIcoreExtension * core,
                  struct glx_config *configs, const __DRIconfig **driConfigs)
{
   struct glx_config head, *tail, *m;

   tail = &head;
   head.next = NULL;
   for (m = configs; m; m = m->next) {
      tail->next = createDriMode(core, m, driConfigs);
      if (tail->next == NULL) {
         /* no matching dri config for m */
         continue;
      }


      tail = tail->next;
   }

   return head.next;
}

_X_HIDDEN void
driDestroyConfigs(const __DRIconfig **configs)
{
   int i;

   for (i = 0; configs[i]; i++)
      free((__DRIconfig *) configs[i]);
   free(configs);
}

static struct glx_config *
driInferDrawableConfig(struct glx_screen *psc, GLXDrawable draw)
{
   unsigned int fbconfig = 0;

   if (__glXGetDrawableAttribute(psc->dpy, draw, GLX_FBCONFIG_ID, &fbconfig)) {
      return glx_config_find_fbconfig(psc->configs, fbconfig);
   }

   return NULL;
}

_X_HIDDEN __GLXDRIdrawable *
driFetchDrawable(struct glx_context *gc, GLXDrawable glxDrawable)
{
   struct glx_display *const priv = __glXInitialize(gc->psc->dpy);
   __GLXDRIdrawable *pdraw;
   struct glx_screen *psc;
   struct glx_config *config = gc->config;

   if (priv == NULL)
      return NULL;

   if (glxDrawable == None)
      return NULL;

   psc = priv->screens[gc->screen];
   if (priv->drawHash == NULL)
      return NULL;

   if (__glxHashLookup(priv->drawHash, glxDrawable, (void *) &pdraw) == 0) {
      pdraw->refcount ++;
      return pdraw;
   }

   if (config == NULL)
      config = driInferDrawableConfig(gc->psc, glxDrawable);
   if (config == NULL)
      return NULL;

   pdraw = psc->driScreen->createDrawable(psc, glxDrawable, glxDrawable,
                                          config);

   if (pdraw == NULL) {
      ErrorMessageF("failed to create drawable\n");
      return NULL;
   }

   if (__glxHashInsert(priv->drawHash, glxDrawable, pdraw)) {
      (*pdraw->destroyDrawable) (pdraw);
      return NULL;
   }
   pdraw->refcount = 1;

   return pdraw;
}

_X_HIDDEN void
driReleaseDrawables(struct glx_context *gc)
{
   const struct glx_display *priv = gc->psc->display;
   __GLXDRIdrawable *pdraw;

   if (priv == NULL)
      return;

   if (__glxHashLookup(priv->drawHash,
		       gc->currentDrawable, (void *) &pdraw) == 0) {
      if (pdraw->drawable == pdraw->xDrawable) {
	 pdraw->refcount --;
	 if (pdraw->refcount == 0) {
	    pdraw->destroyDrawable(pdraw);
	    __glxHashDelete(priv->drawHash, gc->currentDrawable);
	 }
      }
   }

   if (__glxHashLookup(priv->drawHash,
		       gc->currentReadable, (void *) &pdraw) == 0) {
      if (pdraw->drawable == pdraw->xDrawable) {
	 pdraw->refcount --;
	 if (pdraw->refcount == 0) {
	    pdraw->destroyDrawable(pdraw);
	    __glxHashDelete(priv->drawHash, gc->currentReadable);
	 }
      }
   }

   gc->currentDrawable = None;
   gc->currentReadable = None;

}

_X_HIDDEN bool
dri2_convert_glx_attribs(unsigned num_attribs, const uint32_t *attribs,
                         unsigned *major_ver, unsigned *minor_ver,
                         uint32_t *render_type, uint32_t *flags, unsigned *api,
                         int *reset, int *release, unsigned *error)
{
   unsigned i;
   bool got_profile = false;
   int no_error = 0;
   uint32_t profile;

   *major_ver = 1;
   *minor_ver = 0;
   *render_type = GLX_RGBA_TYPE;
   *reset = __DRI_CTX_RESET_NO_NOTIFICATION;
   *release = __DRI_CTX_RELEASE_BEHAVIOR_FLUSH;
   *flags = 0;
   *api = __DRI_API_OPENGL;

   if (num_attribs == 0) {
      return true;
   }

   /* This is actually an internal error, but what the heck.
    */
   if (attribs == NULL) {
      *error = __DRI_CTX_ERROR_UNKNOWN_ATTRIBUTE;
      return false;
   }

   for (i = 0; i < num_attribs; i++) {
      switch (attribs[i * 2]) {
      case GLX_CONTEXT_MAJOR_VERSION_ARB:
	 *major_ver = attribs[i * 2 + 1];
	 break;
      case GLX_CONTEXT_MINOR_VERSION_ARB:
	 *minor_ver = attribs[i * 2 + 1];
	 break;
      case GLX_CONTEXT_FLAGS_ARB:
	 *flags = attribs[i * 2 + 1];
	 break;
      case GLX_CONTEXT_OPENGL_NO_ERROR_ARB:
	 no_error = attribs[i * 2 + 1];
	 break;
      case GLX_CONTEXT_PROFILE_MASK_ARB:
	 profile = attribs[i * 2 + 1];
	 got_profile = true;
	 break;
      case GLX_RENDER_TYPE:
         *render_type = attribs[i * 2 + 1];
	 break;
      case GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB:
         switch (attribs[i * 2 + 1]) {
         case GLX_NO_RESET_NOTIFICATION_ARB:
            *reset = __DRI_CTX_RESET_NO_NOTIFICATION;
            break;
         case GLX_LOSE_CONTEXT_ON_RESET_ARB:
            *reset = __DRI_CTX_RESET_LOSE_CONTEXT;
            break;
         default:
            *error = __DRI_CTX_ERROR_UNKNOWN_ATTRIBUTE;
            return false;
         }
         break;
      case GLX_CONTEXT_RELEASE_BEHAVIOR_ARB:
         switch (attribs[i * 2 + 1]) {
         case GLX_CONTEXT_RELEASE_BEHAVIOR_NONE_ARB:
            *release = __DRI_CTX_RELEASE_BEHAVIOR_NONE;
            break;
         case GLX_CONTEXT_RELEASE_BEHAVIOR_FLUSH_ARB:
            *release = __DRI_CTX_RELEASE_BEHAVIOR_FLUSH;
            break;
         default:
            *error = __DRI_CTX_ERROR_UNKNOWN_ATTRIBUTE;
            return false;
         }
         break;
      default:
	 /* If an unknown attribute is received, fail.
	  */
	 *error = __DRI_CTX_ERROR_UNKNOWN_ATTRIBUTE;
	 return false;
      }
   }

   if (no_error) {
      *flags |= __DRI_CTX_FLAG_NO_ERROR;
   }

   if (!got_profile) {
      if (*major_ver > 3 || (*major_ver == 3 && *minor_ver >= 2))
	 *api = __DRI_API_OPENGL_CORE;
   } else {
      switch (profile) {
      case GLX_CONTEXT_CORE_PROFILE_BIT_ARB:
	 /* There are no profiles before OpenGL 3.2.  The
	  * GLX_ARB_create_context_profile spec says:
	  *
	  *     "If the requested OpenGL version is less than 3.2,
	  *     GLX_CONTEXT_PROFILE_MASK_ARB is ignored and the functionality
	  *     of the context is determined solely by the requested version."
	  */
	 *api = (*major_ver > 3 || (*major_ver == 3 && *minor_ver >= 2))
	    ? __DRI_API_OPENGL_CORE : __DRI_API_OPENGL;
	 break;
      case GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB:
	 *api = __DRI_API_OPENGL;
	 break;
      case GLX_CONTEXT_ES_PROFILE_BIT_EXT:
         if (*major_ver >= 3)
            *api = __DRI_API_GLES3;
         else if (*major_ver == 2 && *minor_ver == 0)
            *api = __DRI_API_GLES2;
         else if (*major_ver == 1 && *minor_ver < 2)
            *api = __DRI_API_GLES;
         else {
            *error = __DRI_CTX_ERROR_BAD_API;
            return false;
         }
         break;
      default:
	 *error = __DRI_CTX_ERROR_BAD_API;
	 return false;
      }
   }

   /* Unknown flag value.
    */
   if (*flags & ~(__DRI_CTX_FLAG_DEBUG | __DRI_CTX_FLAG_FORWARD_COMPATIBLE
                  | __DRI_CTX_FLAG_ROBUST_BUFFER_ACCESS
                  | __DRI_CTX_FLAG_NO_ERROR)) {
      *error = __DRI_CTX_ERROR_UNKNOWN_FLAG;
      return false;
   }

   /* There are no forward-compatible contexts before OpenGL 3.0.  The
    * GLX_ARB_create_context spec says:
    *
    *     "Forward-compatible contexts are defined only for OpenGL versions
    *     3.0 and later."
    */
   if (*major_ver < 3 && (*flags & __DRI_CTX_FLAG_FORWARD_COMPATIBLE) != 0) {
      *error = __DRI_CTX_ERROR_BAD_FLAG;
      return false;
   }

   if (*major_ver >= 3 && *render_type == GLX_COLOR_INDEX_TYPE) {
      *error = __DRI_CTX_ERROR_BAD_FLAG;
      return false;
   }

   *error = __DRI_CTX_ERROR_SUCCESS;
   return true;
}

_X_HIDDEN bool
dri2_check_no_error(uint32_t flags, struct glx_context *share_context,
                    int major, unsigned *error)
{
   Bool noError = flags & __DRI_CTX_FLAG_NO_ERROR;

   /* The KHR_no_error specs say:
    *
    *    Requires OpenGL ES 2.0 or OpenGL 2.0.
    */
   if (noError && major < 2) {
      *error = __DRI_CTX_ERROR_UNKNOWN_ATTRIBUTE;
      return false;
   }

   /* The GLX_ARB_create_context_no_error specs say:
    *
    *    BadMatch is generated if the value of GLX_CONTEXT_OPENGL_NO_ERROR_ARB
    *    used to create <share_context> does not match the value of
    *    GLX_CONTEXT_OPENGL_NO_ERROR_ARB for the context being created.
    */
   if (share_context && !!share_context->noError != !!noError) {
      *error = __DRI_CTX_ERROR_BAD_FLAG;
      return false;
   }

   /* The GLX_ARB_create_context_no_error specs say:
    *
    *    BadMatch is generated if the GLX_CONTEXT_OPENGL_NO_ERROR_ARB is TRUE at
    *    the same time as a debug or robustness context is specified.
    *
    */
   if (noError && ((flags & __DRI_CTX_FLAG_DEBUG) ||
                   (flags & __DRI_CTX_FLAG_ROBUST_BUFFER_ACCESS))) {
      *error = __DRI_CTX_ERROR_BAD_FLAG;
      return false;
   }

   return true;
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

static char *
get_driver_config(const char *driverName)
{
   void *handle;
   char *config = NULL;
   const __DRIextension **extensions = driOpenDriver(driverName, &handle);
   if (extensions) {
      for (int i = 0; extensions[i]; i++) {
         if (strcmp(extensions[i]->name, __DRI_CONFIG_OPTIONS) != 0)
            continue;

         __DRIconfigOptionsExtension *ext =
            (__DRIconfigOptionsExtension *)extensions[i];

         if (ext->base.version >= 2)
            config = ext->getXml(driverName);
         else
            config = strdup(ext->xml);

         break;
      }
   }

   if (!config) {
      /* Fall back to the old method */
      config = dlsym(handle, "__driConfigOptions");
      if (config)
         config = strdup(config);
   }

   dlclose(handle);

   return config;
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

   e->config = get_driver_config(driverName);
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

#endif /* GLX_DIRECT_RENDERING */
