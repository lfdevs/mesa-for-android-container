/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
 * Copyright 2009-2010 Chia-I Wu <olvaffe@gmail.com>
 * Copyright 2010-2011 LunarG, Inc.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


/**
 * Functions for choosing and opening/loading device drivers.
 */


#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "c11/threads.h"

#include "egldefines.h"
#include "egldisplay.h"
#include "egldriver.h"
#include "egllog.h"

#include "util/debug.h"

extern const _EGLDriver _eglDriver;

/**
 * Initialize the display using the driver's function.
 * If the initialisation fails, try again using only software rendering.
 */
bool
_eglInitializeDisplay(_EGLDisplay *disp)
{
   assert(!disp->Initialized);

   /* set options */
   disp->Options.ForceSoftware =
      env_var_as_boolean("LIBGL_ALWAYS_SOFTWARE", false);
   if (disp->Options.ForceSoftware)
      _eglLog(_EGL_DEBUG, "Found 'LIBGL_ALWAYS_SOFTWARE' set, will use a CPU renderer");

   if (_eglDriver.Initialize(&_eglDriver, disp)) {
      disp->Driver = &_eglDriver;
      disp->Initialized = EGL_TRUE;
      return true;
   }

   if (disp->Options.ForceSoftware)
      return false;

   disp->Options.ForceSoftware = EGL_TRUE;
   if (!_eglDriver.Initialize(&_eglDriver, disp))
      return false;

   disp->Driver = &_eglDriver;
   disp->Initialized = EGL_TRUE;
   return true;
}

__eglMustCastToProperFunctionPointerType
_eglGetDriverProc(const char *procname)
{
   if (_eglDriver.GetProcAddress)
      return _eglDriver.GetProcAddress(&_eglDriver, procname);

   return NULL;
}
