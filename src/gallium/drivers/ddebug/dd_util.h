/**************************************************************************
 *
 * Copyright 2015 Advanced Micro Devices, Inc.
 * Copyright 2008 VMware, Inc.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef DD_UTIL_H
#define DD_UTIL_H

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>

#include "os/os_process.h"
#include "util/u_debug.h"

/* name of the directory in home */
#define DD_DIR "ddebug_dumps"

static inline FILE *
dd_get_debug_file(bool verbose)
{
   static unsigned index;
   char proc_name[128], dir[256], name[512];
   FILE *f;

   if (!os_get_process_name(proc_name, sizeof(proc_name))) {
      fprintf(stderr, "dd: can't get the process name\n");
      return NULL;
   }

   snprintf(dir, sizeof(dir), "%s/"DD_DIR, debug_get_option("HOME", "."));

   if (mkdir(dir, 0774) && errno != EEXIST) {
      fprintf(stderr, "dd: can't create a directory (%i)\n", errno);
      return NULL;
   }

   snprintf(name, sizeof(name), "%s/%s_%u_%08u", dir, proc_name, getpid(), index++);
   f = fopen(name, "w");
   if (!f) {
      fprintf(stderr, "dd: can't open file %s\n", name);
      return NULL;
   }

   if (verbose)
      fprintf(stderr, "dd: dumping to file %s\n", name);

   return f;
}

#endif /* DD_UTIL_H */
