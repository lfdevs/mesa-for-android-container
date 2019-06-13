/*
 * Copyright (C) 2018-2019 Lima Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <stdio.h>
#include <stdarg.h>
#include <time.h>

#include <pipe/p_defines.h>

#include "lima_util.h"

FILE *lima_dump_command_stream = NULL;

bool lima_get_absolute_timeout(uint64_t *timeout)
{
   struct timespec current;
   uint64_t current_ns;

   if (*timeout == PIPE_TIMEOUT_INFINITE)
      return true;

   if (clock_gettime(CLOCK_MONOTONIC, &current))
      return false;

   current_ns = ((uint64_t)current.tv_sec) * 1000000000ull;
   current_ns += current.tv_nsec;
   *timeout += current_ns;

   return true;
}

void lima_dump_blob(FILE *fp, void *data, int size, bool is_float)
{
   for (int i = 0; i * 4 < size; i++) {
      if (i % 4 == 0) {
         if (i) fprintf(fp, "\n");
         fprintf(fp, "%04x:", i * 4);
      }

      if (is_float)
         fprintf(fp, " %f", ((float *)data)[i]);
      else
         fprintf(fp, " 0x%08x", ((uint32_t *)data)[i]);
   }
   fprintf(fp, "\n");
}

void
lima_dump_command_stream_print(void *data, int size, bool is_float,
                               const char *fmt, ...)
{
   if (lima_dump_command_stream) {
      va_list ap;
      va_start(ap, fmt);
      vfprintf(lima_dump_command_stream, fmt, ap);
      va_end(ap);

      lima_dump_blob(lima_dump_command_stream, data, size, is_float);
   }
}
