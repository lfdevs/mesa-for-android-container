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
#include "swr_screen.h"

#include <stdio.h>

struct pipe_screen *
swr_create_screen(struct sw_winsys *winsys)
{
   char filename[256] = { 0 };
   fprintf(stderr, "SWR detected ");

   util_dl_library *pLibrary = nullptr;

   util_cpu_detect();

   if (!strlen(filename) &&
       util_cpu_caps.has_avx512f && util_cpu_caps.has_avx512er) {
#if HAVE_SWR_KNL
      fprintf(stderr, "KNL ");
      sprintf(filename, "%s%s%s", UTIL_DL_PREFIX, "swrKNL", UTIL_DL_EXT);
#else
      fprintf(stderr, "KNL (not built) ");
#endif
   }

   if (!strlen(filename) &&
       util_cpu_caps.has_avx512f && util_cpu_caps.has_avx512bw) {
#if HAVE_SWR_SKX
      fprintf(stderr, "SKX ");
      sprintf(filename, "%s%s%s", UTIL_DL_PREFIX, "swrSKX", UTIL_DL_EXT);
#else
      fprintf(stderr, "SKX (not built) ");
#endif
   }

   if (!strlen(filename) && util_cpu_caps.has_avx2) {
#if HAVE_SWR_AVX2
      fprintf(stderr, "AVX2 ");
      sprintf(filename, "%s%s%s", UTIL_DL_PREFIX, "swrAVX2", UTIL_DL_EXT);
#else
      fprintf(stderr, "AVX2 (not built) ");
#endif
   }

   if (!strlen(filename) && util_cpu_caps.has_avx) {
#if HAVE_SWR_AVX
      fprintf(stderr, "AVX ");
      sprintf(filename, "%s%s%s", UTIL_DL_PREFIX, "swrAVX", UTIL_DL_EXT);
#else
      fprintf(stderr, "AVX (not built) ");
#endif
   }

   if (!strlen(filename)) {
      fprintf(stderr, "- no appropriate swr architecture library.  Aborting!\n");
      exit(-1);
   } else {
      fprintf(stderr, "\n");
   }

   pLibrary = util_dl_open(filename);

   if (!pLibrary) {
      fprintf(stderr, "SWR library load failure: %s\n", util_dl_error());
      exit(-1);
   }

   util_dl_proc pApiProc = util_dl_get_proc_address(pLibrary, "SwrGetInterface");

   if (!pApiProc) {
      fprintf(stderr, "SWR library search failure: %s\n", util_dl_error());
      exit(-1);
   }

   struct pipe_screen *screen = swr_create_screen_internal(winsys);
   swr_screen(screen)->pfnSwrGetInterface = (PFNSwrGetInterface)pApiProc;

   return screen;
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
