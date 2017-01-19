/****************************************************************************
 * Copyright (C) 2016 Intel Corporation.   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ***************************************************************************/

#include "util/u_cpu_detect.h"
#include "util/u_dl.h"
#include "swr_public.h"

#include "pipe/p_screen.h"

#include <stdio.h>

typedef pipe_screen *(*screen_create_proc)(struct sw_winsys *winsys);

struct pipe_screen *
swr_create_screen(struct sw_winsys *winsys)
{
   char filename[256];
   fprintf(stderr, "SWR detected ");

   util_dl_library *pLibrary = nullptr;

   util_cpu_detect();
   if (util_cpu_caps.has_avx2) {
      fprintf(stderr, "AVX2\n");
      sprintf(filename, "%s%s%s", UTIL_DL_PREFIX, "swrAVX2", UTIL_DL_EXT);
   } else if (util_cpu_caps.has_avx) {
      fprintf(stderr, "AVX\n");
      sprintf(filename, "%s%s%s", UTIL_DL_PREFIX, "swrAVX", UTIL_DL_EXT);
   } else {
      fprintf(stderr, "no AVX/AVX2 support.  Aborting!\n");
      exit(-1);
   }
   pLibrary = util_dl_open(filename);

   if (!pLibrary) {
      fprintf(stderr, "SWR library load failure: %s\n", util_dl_error());
      exit(-1);
   }

   util_dl_proc pScreenProc = util_dl_get_proc_address(pLibrary, "swr_create_screen_internal");

   if (!pScreenProc) {
      fprintf(stderr, "SWR library search failure: %s\n", util_dl_error());
      exit(-1);
   }

   screen_create_proc pScreenCreate = (screen_create_proc)pScreenProc;

   return pScreenCreate(winsys);
}


#ifdef _WIN32
// swap function called from libl_gdi.c

void
swr_gdi_swap(struct pipe_screen *screen,
             struct pipe_resource *res,
             void *hDC)
{
   screen->flush_frontbuffer(screen,
                             res,
                             0, 0,
                             hDC,
                             NULL);
}

#endif /* _WIN32 */
