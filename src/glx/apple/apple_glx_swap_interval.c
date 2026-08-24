/*
 * Copyright (c) 2026 Apple Inc.
 * SPDX-License-Identifier: MIT
 */

/**
 * \file apple_glx_swap_interval.c
 *
 * \brief GLX swap control for the AppleGL back end, implemented on top
 * of CGL's kCGLCPSwapInterval context parameter.
 */

#include <limits.h>

#include "glxclient.h"
#include "glxextensions.h"
#include "glx_error.h"

#include "apple_glx_context.h"

/**
 * Return the current context if it belongs to the AppleGL back end,
 * otherwise NULL.
 *
 * The swap control code is compiled in for every macOS build, but
 * AppleGL is elected at runtime and the screen-creation walk can fall
 * through to drisw or zink instead (see the GALLIUM_DRIVER and
 * MESA_LOADER_DRIVER_OVERRIDE handling in glxext.c).  On those back ends
 * gc->driContext points at a struct dri_context, so it must not be
 * passed to the apple_glx_context_* helpers.
 */
struct glx_context *
apple_glx_get_current_context(void)
{
   struct glx_context *gc = __glXGetCurrentContext();

   if (gc == &dummyContext || !gc->driContext)
      return NULL;

   if (!gc->psc || !gc->psc->display ||
       gc->psc->display->driver != GLX_DRIVER_APPLEGL)
      return NULL;

   return gc;
}

/**
 * Common back end for the swap-control entry points.
 *
 * A negative interval is rejected rather than passed through, since CGL
 * would wrap it to a very large interval; GLX_EXT_swap_control_tear is
 * not advertised on this path.  Larger values are clamped, as
 * MESA_swap_control requires, so that they throttle rather than
 * wrapping around into no-vsync.
 */
static int
set_swap_interval(int interval)
{
   struct glx_context *gc = apple_glx_get_current_context();

   if (!gc)
      return GLX_BAD_CONTEXT;

   if (interval < 0)
      return GLX_BAD_VALUE;

   if (interval > APPLE_GLX_MAX_SWAP_INTERVAL)
      interval = APPLE_GLX_MAX_SWAP_INTERVAL;

   if (!apple_glx_context_set_swap_interval(gc->driContext, interval))
      return GLX_BAD_CONTEXT;

   return 0;
}

/*
** GLX_SGI_swap_control
*/
int
glXSwapIntervalSGI(int interval)
{
   /* Unlike the MESA and EXT variants, SGI_swap_control cannot disable
    * vsync.  Check the context first so a bad-context error takes
    * precedence, matching the DRI implementation in glxcmds.c.
    */
   if (!apple_glx_get_current_context())
      return GLX_BAD_CONTEXT;

   if (interval <= 0)
      return GLX_BAD_VALUE;

   return set_swap_interval(interval);
}


/*
** GLX_MESA_swap_control
*/
int
glXSwapIntervalMESA(unsigned int interval)
{
   /* Reject rather than clamp here, matching the DRI implementation;
    * anything in range is clamped by set_swap_interval().
    */
   if (interval > INT_MAX)
      return GLX_BAD_VALUE;

   return set_swap_interval(interval);
}


/**
 * Note that CGL defaults kCGLCPSwapInterval to 1, whereas
 * MESA_swap_control specifies a default of 0.  Report what CGL is
 * actually doing rather than forcing 0 at context creation, which would
 * silently un-vsync every existing AppleGL application.
 */
int
glXGetSwapIntervalMESA(void)
{
   struct glx_context *gc = apple_glx_get_current_context();
   int interval;

   if (!gc)
      return 0;

   if (!apple_glx_context_get_swap_interval(gc->driContext, &interval))
      return 0;

   return interval;
}


/*
** GLX_EXT_swap_control
*/
void
glXSwapIntervalEXT(Display * dpy, GLXDrawable drawable, int interval)
{
   (void) drawable;

   /* CGL only exposes the swap interval per context, not per drawable,
    * so this applies to the calling thread's current context rather
    * than to `drawable`.
    *
    * GLX_EXT_swap_control_tear is not advertised, so a negative interval
    * is a BadValue.  Report it rather than dropping it silently.
    */
   if (set_swap_interval(interval) == GLX_BAD_VALUE)
      __glXSendError(dpy, BadValue, interval, 0, True);
}
