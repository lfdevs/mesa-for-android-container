/*
 * Copyright © 2024 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "freedreno_rd_output.h"

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "c11/threads.h"
#include "util/detect_os.h"
#include "util/log.h"
#include "util/u_atomic.h"
#include "util/u_debug.h"

#if DETECT_OS_ANDROID
static const char *fd_rd_output_base_path = "/data/local/tmp";
#else
static const char *fd_rd_output_base_path = "/tmp";
#endif

static const struct debug_control fd_rd_dump_options[] = {
   { "enable", FD_RD_DUMP_ENABLE },
   { "combine", FD_RD_DUMP_COMBINE },
   { "full", FD_RD_DUMP_FULL },
   { "trigger", FD_RD_DUMP_TRIGGER },
   { NULL, 0 }
};

struct fd_rd_dump_env fd_rd_dump_env;

struct fd_rd_dump_range {
   unsigned range_begin;
   unsigned range_end;
};

static void
fd_rd_dump_env_init_once(void)
{
   fd_rd_dump_env.flags = parse_debug_string(os_get_option("FD_RD_DUMP"),
                                             fd_rd_dump_options);

   /* If any of the more-detailed FD_RD_DUMP flags is enabled, the general
    * FD_RD_DUMP_ENABLE flag should also implicitly be set.
    */
   if (fd_rd_dump_env.flags & ~FD_RD_DUMP_ENABLE)
      fd_rd_dump_env.flags |= FD_RD_DUMP_ENABLE;
}

void
fd_rd_dump_env_init(void)
{
   static once_flag once = ONCE_FLAG_INIT;
   call_once(&once, fd_rd_dump_env_init_once);
}

static void
fd_rd_output_sanitize_name(char *name)
{
   /* The name string is null-terminated after being constructed via asprintf.
    * Sanitize it by reducing to an underscore anything that's not a hyphen,
    * underscore, dot or alphanumeric character.
    */
   for (char *s = name; *s; ++s) {
      if (isalnum(*s) || *s == '-' || *s == '_' || *s == '.')
         continue;
      *s = '_';
   }
}

static void
fd_rd_parse_dump_range(const char *option_name, struct util_dynarray *range_array)
{
   *range_array = UTIL_DYNARRAY_INIT;
   const char *range_value = os_get_option(option_name);
   if (!range_value)
      return;

   const char *p = range_value;
   while (p && *p) {
      char *ep = NULL;

      struct fd_rd_dump_range range;
      range.range_begin = strtol(p, &ep, 0);
      if (ep == p)
         break;
      p = ep;

      range.range_end = range.range_begin;
      if (*p == '-') {
         ep = NULL;
         range.range_end = strtol(++p, &ep, 0);
         if (ep == p)
            break;
         p = ep;
      }

      util_dynarray_append(range_array, range);

      if (*p == ',')
         ++p;
      if (!isdigit(*p) && !!*p)
         break;
   }

   if (*p == '\0') {
      mesa_logi("[fd_rd_output] %s specified %" PRIuPTR " dump ranges:",
                option_name, util_dynarray_num_elements(range_array, struct fd_rd_dump_range));
      util_dynarray_foreach(range_array, struct fd_rd_dump_range, range) {
         mesa_logi("[fd_rd_output]   [%u, %u]", range->range_begin, range->range_end);
      }
   } else {
      mesa_logi("[fd_rd_output] failed to parse dump range '%s' for %s",
                range_value, option_name);

      util_dynarray_clear(range_array);
      struct fd_rd_dump_range invalid_range = {
         .range_begin = UINT_MAX,
         .range_end = UINT_MAX,
      };
      util_dynarray_append(range_array, invalid_range);
   }
}

static bool
fd_rd_output_allowed(struct fd_rd_output *output, uint32_t frame, uint32_t submit)
{
   /* Allow output if no ranges were specified. */
   if (!util_dynarray_num_elements(&output->frame_ranges, struct fd_rd_dump_range) &&
       !util_dynarray_num_elements(&output->submit_ranges, struct fd_rd_dump_range))
      return true;

   util_dynarray_foreach(&output->frame_ranges, struct fd_rd_dump_range, range) {
      if (frame >= range->range_begin && frame <= range->range_end)
         return true;
   }

   util_dynarray_foreach(&output->submit_ranges, struct fd_rd_dump_range, range) {
      if (submit >= range->range_begin && submit <= range->range_end)
         return true;
   }

   return false;
}

void
fd_rd_output_init(struct fd_rd_output *output, const char* output_name)
{
   const char *test_name = os_get_option("FD_RD_DUMP_TESTNAME");
   ASSERTED int name_len;
   if (test_name)
      name_len = asprintf(&output->name, "%s_%s", test_name, output_name);
   else
      name_len = asprintf(&output->name, "%s", output_name);
   assert(name_len != -1);
   fd_rd_output_sanitize_name(output->name);

   output->combine = false;
   output->file = NULL;
   output->trigger_fd = -1;
   output->trigger_count = 0;

   if (FD_RD_DUMP(COMBINE)) {
      output->combine = true;

      char file_path[PATH_MAX];
      snprintf(file_path, sizeof(file_path), "%s/%s_combined.rd.gz",
               fd_rd_output_base_path, output->name);
      output->file = gzopen(file_path, "w");
   }

   if (FD_RD_DUMP(TRIGGER)) {
      char file_path[PATH_MAX];
      snprintf(file_path, sizeof(file_path), "%s/%s_trigger",
               fd_rd_output_base_path, output->name);
      output->trigger_fd = open(file_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
   }

   fd_rd_parse_dump_range("FD_RD_DUMP_FRAMES", &output->frame_ranges);
   fd_rd_parse_dump_range("FD_RD_DUMP_SUBMITS", &output->submit_ranges);
}

void
fd_rd_output_fini(struct fd_rd_output *output)
{
   free(output->name);

   if (output->file != NULL) {
      assert(output->combine);
      gzclose(output->file);
   }

   if (output->trigger_fd >= 0) {
      close(output->trigger_fd);

      /* Remove the trigger file. The filename is reconstructed here
       * instead of having to spend memory to store it in the struct.
       */
      char file_path[PATH_MAX];
      snprintf(file_path, sizeof(file_path), "%s/%s_trigger",
               fd_rd_output_base_path, output->name);
      unlink(file_path);
   }

   util_dynarray_fini(&output->frame_ranges);
   util_dynarray_fini(&output->submit_ranges);
}

static void
fd_rd_output_update_trigger_count(struct fd_rd_output *output)
{
   assert(FD_RD_DUMP(TRIGGER));

   /* Retrieve the trigger file size, only attempt to update the trigger
    * value if anything was actually written to that file.
    */
   struct stat stat;
   if (fstat(output->trigger_fd, &stat) != 0) {
      mesa_loge("[fd_rd_output] failed to acccess the %s trigger file",
                output->name);
      return;
   }

   if (stat.st_size == 0)
      return;

   char trigger_data[32];
   int ret = read(output->trigger_fd, trigger_data, sizeof(trigger_data));
   if (ret < 0) {
      mesa_loge("[fd_rd_output] failed to read from the %s trigger file",
                output->name);
      return;
   }
   int num_read = MIN2(ret, sizeof(trigger_data) - 1);

   /* After reading from it, the trigger file should be reset, which means
    * moving the file offset to the start of the file as well as truncating
    * it to zero bytes.
    */
   if (lseek(output->trigger_fd, 0, SEEK_SET) < 0) {
      mesa_loge("[fd_rd_output] failed to reset the %s trigger file position",
                output->name);
      return;
   }

   if (ftruncate(output->trigger_fd, 0) < 0) {
      mesa_loge("[fd_rd_output] failed to truncate the %s trigger file",
                output->name);
      return;
   }

   /* Try to decode the count value through strtol. -1 translates to UINT_MAX
    * and keeps generating dumps until disabled. Any positive value will
    * allow generating dumps for that many submits. Any other value will
    * disable any further generation of RD dumps.
    */
   trigger_data[num_read] = '\0';
   int32_t value = strtol(trigger_data, NULL, 0);

   if (value == -1) {
      output->trigger_count = UINT_MAX;
      mesa_logi("[fd_rd_output] %s trigger enabling RD dumps until disabled",
                output->name);
   } else if (value > 0) {
      output->trigger_count = (uint32_t) value;
      mesa_logi("[fd_rd_output] %s trigger enabling RD dumps for next %u submissions",
                output->name, output->trigger_count);
   } else {
      output->trigger_count = 0;
      mesa_logi("[fd_rd_output] %s trigger disabling RD dumps", output->name);
   }
}

bool
fd_rd_output_begin(struct fd_rd_output *output, uint32_t frame, uint32_t submit)
{
   assert(output->combine ^ (output->file == NULL));

   if (FD_RD_DUMP(TRIGGER)) {
      fd_rd_output_update_trigger_count(output);

      if (output->trigger_count == 0)
         return false;
      /* UINT_MAX corresponds to generating dumps until disabled. */
      if (output->trigger_count != UINT_MAX)
          --output->trigger_count;
   }

   if (!fd_rd_output_allowed(output, frame, submit))
      return false;

   if (output->combine)
      return true;

   char file_path[PATH_MAX];
   if (frame != UINT_MAX) {
      snprintf(file_path, sizeof(file_path), "%s/%s_frame%.5d_submit%.5d.rd",
               fd_rd_output_base_path, output->name, frame, submit);
   } else {
      snprintf(file_path, sizeof(file_path), "%s/%s_submit%.5d.rd",
               fd_rd_output_base_path, output->name, submit);
   }
   output->file = gzopen(file_path, "w");
   return true;
}

static void
fd_rd_output_write(struct fd_rd_output *output, const void *buffer, int size)
{
   const uint8_t *pos = (uint8_t *) buffer;
   while (size > 0) {
      int ret = gzwrite(output->file, pos, size);
      if (ret < 0) {
         mesa_loge("[fd_rd_output] failed to write to compressed output: %s",
                   gzerror(output->file, NULL));
         return;
      }
      pos += ret;
      size -= ret;
   }
}

void
fd_rd_output_write_section(struct fd_rd_output *output, enum rd_sect_type type,
                           const void *buffer, int size)
{
   fd_rd_output_write(output, &type, 4);
   fd_rd_output_write(output, &size, 4);
   fd_rd_output_write(output, buffer, size);
}

void
fd_rd_output_end(struct fd_rd_output *output)
{
   assert(output->file != NULL);

   /* When combining output, flush the gzip stream on each submit. This should
    * store all the data before any problem during the submit itself occurs.
    */
   if (output->combine) {
      gzflush(output->file, Z_FINISH);
      return;
   }

   gzclose(output->file);
   output->file = NULL;
}
