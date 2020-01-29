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

#include "util/u_debug.h"

#include "lima_util.h"
#include "lima_parser.h"

FILE *lima_dump_command_stream = NULL;
int lima_dump_frame_count = 0;

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
   fprintf(fp, "{\n");
   for (int i = 0; i * 4 < size; i++) {
      if (i % 4 == 0)
         fprintf(fp, "\t");

      if (is_float)
         fprintf(fp, "%f, ", ((float *)data)[i]);
      else
         fprintf(fp, "0x%08x, ", ((uint32_t *)data)[i]);

      if ((i % 4 == 3) || (i == size / 4 - 1)) {
         fprintf(fp, "/* 0x%08x */", MAX2((i - 3) * 4, 0));
         if (i) fprintf(fp, "\n");
      }
   }
   fprintf(fp, "}\n");
}

void
lima_dump_vs_command_stream_print(void *data, int size, uint32_t start)
{
   if (lima_dump_command_stream)
      lima_parse_vs(lima_dump_command_stream, (uint32_t *)data, size, start);
}

void
lima_dump_plbu_command_stream_print(void *data, int size, uint32_t start)
{
   if (lima_dump_command_stream)
      lima_parse_plbu(lima_dump_command_stream, (uint32_t *)data, size, start);
}

void
lima_dump_rsw_command_stream_print(void *data, int size, uint32_t start)
{
   if (lima_dump_command_stream)
      lima_parse_render_state(lima_dump_command_stream, (uint32_t *)data, size, start);
}

void
lima_dump_texture_descriptor(void *data, int size, uint32_t start, uint32_t offset)
{
   if (lima_dump_command_stream)
      lima_parse_texture_descriptor(lima_dump_command_stream, (uint32_t *)data, size, start, offset);
}

void
lima_dump_file_open(void)
{
   if (lima_dump_command_stream)
      return;

   char buffer[1024];
   const char *dump_command = debug_get_option("LIMA_DUMP_FILE", "lima.dump");
   snprintf(buffer, sizeof(buffer), "%s.%04d", dump_command, lima_dump_frame_count);

   printf("lima: dump command stream to file %s\n", buffer);
   lima_dump_command_stream = fopen(buffer, "w");
   if (!lima_dump_command_stream)
      fprintf(stderr, "lima: failed to open command stream log file %s\n",
              buffer);
}

void
lima_dump_file_close(void)
{
   if (lima_dump_command_stream) {
      fclose(lima_dump_command_stream);
      lima_dump_command_stream = NULL;
   }
}

void
lima_dump_file_next(void)
{
   if (lima_dump_command_stream) {
      lima_dump_file_close();
      lima_dump_frame_count++;
      lima_dump_file_open();
   }
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
