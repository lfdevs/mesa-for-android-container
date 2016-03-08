/**************************************************************************
 *
 * Copyright 2013 Advanced Micro Devices, Inc.
 * All Rights Reserved.
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
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/*
 * Authors:
 *      Christian König <christian.koenig@amd.com>
 *
 */

#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include <X11/Xlib.h>

#include "os/os_thread.h"
#include "util/u_memory.h"
#include "loader/loader.h"

#include "entrypoint.h"
#include "vid_dec.h"
#include "vid_enc.h"

pipe_static_mutex(omx_lock);
static Display *omx_display = NULL;
static struct vl_screen *omx_screen = NULL;
static unsigned omx_usecount = 0;
static const char *omx_render_node = NULL;
static int drm_fd;

int omx_component_library_Setup(stLoaderComponentType **stComponents)
{
   OMX_ERRORTYPE r;

   if (stComponents == NULL)
      return 2;

   /* component 0 - video decoder */
   r = vid_dec_LoaderComponent(stComponents[0]);
   if (r != OMX_ErrorNone)
      return OMX_ErrorInsufficientResources;

   /* component 1 - video encoder */
   r = vid_enc_LoaderComponent(stComponents[1]);
   if (r != OMX_ErrorNone)
      return OMX_ErrorInsufficientResources;

   return 2;
}

struct vl_screen *omx_get_screen(void)
{
   static bool first_time = true;
   pipe_mutex_lock(omx_lock);

   if (!omx_screen) {
      if (first_time) {
         omx_render_node = debug_get_option("OMX_RENDER_NODE", NULL);
         first_time = false;
      }
      if (omx_render_node) {
         drm_fd = loader_open_device(omx_render_node);
         if (drm_fd < 0)
            goto error;

         omx_screen = vl_drm_screen_create(drm_fd);
         if (!omx_screen) {
            close(drm_fd);
            goto error;
         }
      } else {
         omx_display = XOpenDisplay(NULL);
         if (!omx_display)
            goto error;

         omx_screen = vl_dri2_screen_create(omx_display, 0);
         if (!omx_screen) {
            XCloseDisplay(omx_display);
            goto error;
         }
      }
   }

   ++omx_usecount;

   pipe_mutex_unlock(omx_lock);
   return omx_screen;

error:
   pipe_mutex_unlock(omx_lock);
   return NULL;
}

void omx_put_screen(void)
{
   pipe_mutex_lock(omx_lock);
   if ((--omx_usecount) == 0) {
      omx_screen->destroy(omx_screen);
      omx_screen = NULL;

      if (omx_render_node)
         close(drm_fd);
      else
         XCloseDisplay(omx_display);
   }
   pipe_mutex_unlock(omx_lock);
}

OMX_ERRORTYPE omx_workaround_Destructor(OMX_COMPONENTTYPE *comp)
{
   omx_base_component_PrivateType* priv = (omx_base_component_PrivateType*)comp->pComponentPrivate;

   priv->state = OMX_StateInvalid;
   tsem_up(priv->messageSem);

   /* wait for thread to exit */
   pthread_join(priv->messageHandlerThread, NULL);

   return omx_base_component_Destructor(comp);
}
