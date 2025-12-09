/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "util/hash_table.h"
#include "util/os_file.h"
#include "util/os_misc.h"
#include "util/ralloc.h"
#include "util/u_dynarray.h"

#include "slice.h"
#include "tar.h"

typedef struct content {
   slice name;
   slice fullname;
   slice data;
} content;

typedef struct object {
   slice prefix;
   slice name;
   slice fullname;

   int      versions_count;
   content *versions;

   struct mesa_archive *ma;
} object;

typedef struct mesa_archive {
   slice   filename;
   slice   contents;
   int     objects_count;
   object *objects;

   const char *info;
   slice detected_mda_prefix;
} mesa_archive;

enum diff_mode {
   DIFF_UNIFIED,
   DIFF_SIDE_BY_SIDE,
};

typedef struct context {
   const char *cmd_name;

   char **args;
   int    args_count;

   mesa_archive **archives;
   int            archives_count;

   struct {
      enum diff_mode mode;
      int            param;
   } diff;
} context;

#define foreach_object(OBJ, MA)                            \
   for (object *OBJ = (MA)->objects;                       \
        OBJ < (MA)->objects + (MA)->objects_count;         \
        OBJ++)

#define foreach_version(CONTENT, OBJ)                      \
   for (content *CONTENT = (OBJ)->versions;                \
        CONTENT < (OBJ)->versions + (OBJ)->versions_count; \
        CONTENT++)

static void PRINTFLIKE(1, 2)
failf(const char *fmt, ...)
{
   fflush(stdout);
   va_list args;
   va_start(args, fmt);
   vfprintf(stderr, fmt, args);
   va_end(args);
   exit(1);
}

typedef struct {
   FILE *f;
   const char *path;
} temp_file;

static temp_file
make_temp_file(void *mem_ctx)
{
   char path[] = "/tmp/fileXXXXXX";

   int fd = mkstemp(path);
   if (fd == -1)
      failf("mda: failed creating temporary file: %s", strerror(errno));

   FILE *f = fdopen(fd, "w");
   if (!f)
      failf("mda: failed creating temporary file: %s", strerror(errno));

   temp_file r = {0};
   r.f = f;
   r.path = ralloc_strdup(mem_ctx, path);
   return r;
}

static void
diff(context *ctx, slice a, slice b)
{
   void *mem_ctx = ralloc_context(NULL);

   temp_file file_a = make_temp_file(mem_ctx);
   temp_file file_b = make_temp_file(mem_ctx);

   fwrite(a.data, a.len, 1, file_a.f);
   fwrite(b.data, b.len, 1, file_b.f);

   fclose(file_a.f);
   fclose(file_b.f);

   const char *diff_cmd = os_get_option("MDA_DIFF_COMMAND");
   if (!diff_cmd) {
      if (ctx->diff.mode == DIFF_UNIFIED) {
         diff_cmd = ralloc_asprintf(mem_ctx, "git diff --no-index --color-words -U%d -- %%s %%s | tail -n +5", ctx->diff.param);
      } else {
         diff_cmd = ralloc_asprintf(mem_ctx, "diff -y -W%d %%s %%s", ctx->diff.param);
      }
   }

   char *cmd = ralloc_asprintf(mem_ctx, diff_cmd, file_a.path, file_b.path);

   /* Make sure everything printed so far is flushed before the diff
    * subprocess print things.
    */
   fflush(stdout);

   system(cmd);

   unlink(file_a.path);
   unlink(file_b.path);
   ralloc_free(mem_ctx);
}

static content *
first_version(object *obj)
{
   assert(obj->versions_count > 0);
   return &obj->versions[0];
}

static content *
last_version(object *obj)
{
   assert(obj->versions_count > 0);
   return &obj->versions[obj->versions_count - 1];
}

static void
print_separator(int count)
{
   /* Negative numbers always result in just a new line*/
   for (; count > 1; count--)
      putchar('#');
   putchar('\n');
}

static mesa_archive *
parse_mesa_archive(void *mem_ctx, const char *filename)
{
   size_t size = 0;
   char *contents = os_read_file(filename, &size);
   if (!contents) {
      fprintf(stderr, "mda: error reading file %s: %s\n", filename, strerror(errno));
      return NULL;
   }

   mesa_archive *ma = rzalloc(mem_ctx, mesa_archive);
   ma->filename = slice_from_cstr(ralloc_strdup(ma, filename));
   ma->contents = (slice) { ralloc_memdup(ma, (const char *)contents, size), size };
   free(contents);

   tar_reader tr = {0};
   tar_reader_init_from_bytes(&tr, ma->contents.data, ma->contents.len);

   tar_reader_entry entry = {0};

   bool found_mesa_txt = false;
   ma->detected_mda_prefix = (slice){};
   while (tar_reader_next(&tr, &entry)) {
      slice fullpath;
      if (!slice_is_empty(entry.prefix)) {
         char *fullpath_str = ralloc_asprintf(ma, "%.*s/%.*s",
                                              SLICE_FMT(entry.prefix),
                                              SLICE_FMT(entry.name));
         fullpath = slice_from_cstr(fullpath_str);
      } else {
         fullpath = entry.name;
      }

      slice mda_mesa_txt = slice_from_cstr("mda/mesa.txt");

      if (slice_ends_with(fullpath, mda_mesa_txt)) {
         slice_cut_result cut = slice_cut(fullpath, '/');
         if (cut.found && slice_equal_cstr(cut.after, "mesa.txt")) {
            /* Cut was succesful, so can extend to include the separator. */
            ma->detected_mda_prefix = (slice){ cut.before.data, cut.before.len+1 };
            ma->info = slice_to_cstr(ma, entry.contents);
            found_mesa_txt = true;
            break;
         }
      }
   }

   if (!found_mesa_txt) {
      fprintf(stderr, "mda: wrong archive, missing mesa.txt\n");
      return NULL;
   }

   /* Now that we found mesa. Reset header. */
   tar_reader_init_from_bytes(&tr, ma->contents.data, ma->contents.len);

   struct hash_table *lookup = slice_hash_table_create(ma);

   while (tar_reader_next(&tr, &entry)) {
      slice fullpath;
      if (!slice_is_empty(entry.prefix)) {
         char *fullpath_str = ralloc_asprintf(ma, "%.*s/%.*s",
                                              SLICE_FMT(entry.prefix),
                                              SLICE_FMT(entry.name));
         fullpath = slice_from_cstr(fullpath_str);
      } else {
         fullpath = entry.name;
      }

      /* Ignore directory entries. */
      if (slice_is_empty(entry.contents))
         continue;

      if (!slice_starts_with(fullpath, ma->detected_mda_prefix)) {
         fprintf(stderr, "mda: ignoring unexpected file with wrong prefix: %.*s\n", SLICE_FMT(fullpath));
         continue;
      }

      /* Remove the detected prefix from paths.  We'll use the filename later
       * on since is more visible to the user.  Most of the time is going to
       * be the same.
       */
      {
         slice_cut_result cut = slice_cut(fullpath, '/');
         assert(cut.found);
         fullpath = cut.after;
      }

      /* Already processed this before. */
      if (slice_equal_cstr(fullpath, "mesa.txt"))
         continue;

      slice_cut_result first_cut = slice_cut(fullpath, '/');
      if (!first_cut.found)
         continue;

      slice prefix_normalized = first_cut.before;
      slice_cut_result second_cut = slice_cut(first_cut.after, '/');

      slice key_slice, object_name, version_name;

      if (second_cut.found) {
         /* Normal format: "0/OBJECT-NAME/version-name". */
         object_name = second_cut.before;
         version_name = second_cut.after;
         key_slice = slice_substr_to(fullpath,
                                     second_cut.before.data + second_cut.before.len - fullpath.data);
      } else {
         /* Single version format: "0/SPIRV". */
         object_name = first_cut.after;
         version_name = slice_from_cstr("binary");
         key_slice = fullpath;
      }

      struct hash_entry *hash_entry = slice_hash_table_search(lookup, key_slice);
      int obj_index = hash_entry ? (intptr_t)hash_entry->data : -1;
      object *obj;

      if (obj_index == -1) {
         ma->objects = rerzalloc(ma, ma->objects, object, ma->objects_count, ma->objects_count + 1);
         obj_index = ma->objects_count++;
         obj = &ma->objects[obj_index];
         obj->prefix = prefix_normalized;
         obj->name = object_name;
         obj->ma = ma;
         char *fullname_str = ralloc_asprintf(ma, "%.*s/%.*s/%.*s",
                                              SLICE_FMT(ma->filename),
                                              SLICE_FMT(prefix_normalized),
                                              SLICE_FMT(object_name));
         obj->fullname = slice_from_cstr(fullname_str);
         obj->versions = NULL;
         obj->versions_count = 0;

         slice_hash_table_insert(lookup, key_slice, (void *)(intptr_t)obj_index);
      } else {
         obj = &ma->objects[obj_index];
      }

      obj->versions = rerzalloc(ma, obj->versions, content,
                               obj->versions_count, obj->versions_count + 1);
      int s = obj->versions_count++;

      obj->versions[s].name = version_name;
      obj->versions[s].data = entry.contents;
      char *version_fullname_str = ralloc_asprintf(ma, "%.*s/%.*s", SLICE_FMT(obj->fullname), SLICE_FMT(version_name));
      obj->versions[s].fullname = slice_from_cstr(version_fullname_str);
   }

   return ma;
}

typedef struct {
   slice    fullname;
   object  *object;
   content *content;
} match;

typedef struct {
   match *matches;
   int    matches_count;
} find_all_result;

enum match_flags {
   /* Up until first slash in the pattern, consider a prefix match, then
    * fuzzy for the remaining of the pattern.
    *
    * This works better for the common case of mda.tar files with names
    * containing hashes.  Trying to disambiguate by a prefix might end up
    * also fuzzy matching the middle of other hashes.
    */
   MATCH_PREFIX_FIRST_SLASH = 1 << 0,
};

static bool
is_match(slice name_slice, const char *pattern, unsigned match_flags)
{
   assert(!slice_is_empty(name_slice));

   slice pattern_slice = slice_from_cstr(pattern);

   /* Non-fuzzy matching first. */
   if (slice_contains_str(name_slice, pattern_slice))
      return true;

   slice s = name_slice;
   slice p = pattern_slice;

   if (match_flags & MATCH_PREFIX_FIRST_SLASH) {
      slice_cut_result pattern_cut = slice_cut(pattern_slice, '/');
      if (pattern_cut.found) {
         slice_cut_result name_cut = slice_cut(name_slice, '/');
         if (!name_cut.found || !slice_starts_with(name_cut.before, pattern_cut.before))
            return false;

         /* Update s and p to continue from after the slash. */
         s = name_cut.after;
         p = pattern_cut.after;
      }
   }

   bool matched = false;
   int s_idx = 0, p_idx = 0;
   while (s_idx < s.len && p_idx < p.len) {
      if (s.data[s_idx] == p.data[p_idx]) {
         p_idx++;
         if (p_idx == p.len) {
            matched = true;
            break;
         }
      }
      s_idx++;
   }

   return matched;
}

static void
append_match(context *ctx, find_all_result *r, object *obj, content *c)
{
   r->matches = rerzalloc(ctx, r->matches, match, r->matches_count, r->matches_count + 1);

   match *m   = &r->matches[r->matches_count++];
   m->fullname    = c ? c->fullname : obj->fullname;
   m->object  = obj;
   m->content = c;
}

static find_all_result
find_all(context *ctx, const char *pattern)
{
   find_all_result r = {};

   if (!pattern)
      pattern = "";

   unsigned round_flags[2] = {};
   unsigned rounds = 1;
   if (strchr(pattern, '/')) {
      /* See comment on the enum definition. */
      round_flags[0] = MATCH_PREFIX_FIRST_SLASH;
      rounds++;
   }

   for (int round = 0; round < rounds; round++) {
      unsigned match_flags = round_flags[round];

      for (int i = 0; i < ctx->archives_count; i++) {
         mesa_archive *ma = ctx->archives[i];

         foreach_object(obj, ma) {
            if (is_match(obj->fullname, pattern, match_flags))
               append_match(ctx, &r, obj, NULL);
         }
      }

      if (r.matches_count > 0)
         return r;

      for (int i = 0; i < ctx->archives_count; i++) {
         mesa_archive *ma = ctx->archives[i];

         foreach_object(obj, ma) {
            foreach_version(c, obj) {
               if (is_match(c->fullname, pattern, match_flags))
                  append_match(ctx, &r, obj, c);
            }
         }
      }

      if (r.matches_count > 0)
         return r;
   }

   return r;
}

static match
find_one(context *ctx, const char *pattern)
{
   find_all_result r = find_all(ctx, pattern);

   if (r.matches_count == 1) {
      return r.matches[0];

   } else if (r.matches_count == 0) {
      fprintf(stderr, "mda: couldn't match pattern: %s\n", pattern);
      return (match){};

   } else {
      assert(r.matches_count > 1);
      fprintf(stderr, "error: multiple matches for pattern: %s\n", pattern);

      for (int i = 0; i < r.matches_count; i++) {
         match *m = &r.matches[i];
         fprintf(stderr, "    %.*s\n", SLICE_FMT(m->fullname));
      }
      return (match){};
   }
}

static int
cmd_info(context *ctx)
{
   for (int i = 0; i < ctx->archives_count; i++) {
      if (i > 0) {
         printf("\n");
      }

      mesa_archive *ma = ctx->archives[i];
      printf("# From %.*s\n", SLICE_FMT(ma->filename));
      printf("%s\n", ma->info);
   }

   return 0;
}

static int
cmd_listraw(context *ctx)
{
   for (int i = 0; i < ctx->archives_count; i++) {
      mesa_archive *ma = ctx->archives[i];

      foreach_object(obj, ma) {
         foreach_version(c, obj) {
            printf("%.*s\n", SLICE_FMT(c->fullname));
         }
      }
   }
   return 0;
}

static int
cmd_list(context *ctx)
{
   bool all = !strcmp(ctx->cmd_name, "listall");

   for (int i = 0; i < ctx->archives_count; i++) {
      if (i > 0) {
         printf("\n");
      }

      mesa_archive *ma = ctx->archives[i];
      printf("%.*s/\n", SLICE_FMT(ma->filename));

      const char *cur_name = "";

      foreach_object(obj, ma) {
         if (!slice_equal_cstr(obj->prefix, cur_name)) {
            printf("  %.*s/\n", SLICE_FMT(obj->prefix));
            cur_name = slice_to_cstr(ctx, obj->prefix);
         }
         printf("    %.*s/", SLICE_FMT(obj->name));
         if (obj->versions_count > 1)
            printf(" (%d versions)", obj->versions_count);
         printf("\n");
         if (all) {
            foreach_version(c, obj) {
               printf("      %.*s\n", SLICE_FMT(c->name));
            }
         }
      }
   }

   return 0;
}

static int
cmd_logsum(context *ctx)
{
   if (ctx->args_count == 0) {
      fprintf(stderr, "mda: need to pass an object to log\n");
      return 1;
   }

   const char *pattern = ctx->args[0];

   match m = find_one(ctx, pattern);
   if (!m.object)
      return 1;


   printf("%.*s/\n", SLICE_FMT(m.object->fullname));

   foreach_version(c, m.object) {
      printf("  %.*s\n", SLICE_FMT(c->name));
   }

   printf("\n");

   return 0;
}

static int
cmd_diff(context *ctx)
{
   if (ctx->args_count != 2 && ctx->args_count != 3) {
      fprintf(stderr, "mda: invalid arguments\n");
      return 1;
   }

   match a = find_one(ctx, ctx->args[0]);
   if (!a.object)
      return 1;

   match b = find_one(ctx, ctx->args[1]);
   if (!b.object)
      return 1;

   if (!a.content)
      a.content = last_version(a.object);
   if (!b.content)
      b.content = last_version(b.object);

   int x = printf("# A: %.*s\n", SLICE_FMT(a.content->fullname));
   int y = printf("# B: %.*s\n", SLICE_FMT(b.content->fullname));
   print_separator(MAX2(x, y));
   printf("\n");

   diff(ctx, a.content->data, b.content->data);
   printf("\n");

   return 0;
}

static int
cmd_log(context *ctx)
{
   if (ctx->args_count != 1 && ctx->args_count != 2) {
      fprintf(stderr, "mda: need to pass one or two patterns to log command\n");
      return 1;
   }

   enum mode {
      MODE_DIFF,
      MODE_ONELINE,
      MODE_FULL,
   };
   enum mode mode = !strcmp(ctx->cmd_name, "logfull") ? MODE_FULL     :
                    !strcmp(ctx->cmd_name, "log1")    ? MODE_ONELINE  :
                                                        MODE_DIFF;

   const char *start_pattern = ctx->args[0];
   const char *end_pattern   = ctx->args_count > 1 ? ctx->args[1]
                                                   : NULL;

   match start = find_one(ctx, start_pattern);
   if (!start.object)
      return 1;
   if (!start.content)
      start.content = first_version(start.object);

   match end = {};
   if (end_pattern) {
      end = find_one(ctx, end_pattern);
      if (!end.object)
         return 1;
      if (!end.content)
         end.content = last_version(end.object);
   } else {
      end = start;
      end.content = last_version(end.object);
   }

   if (start.object != end.object)
      failf("can't log between two different objects");
   object *obj = start.object;

   if (mode == MODE_ONELINE) {
      printf("%.*s/\n", SLICE_FMT(obj->fullname));
      for (const content *curr = start.content; curr <= end.content; curr++) {
         printf("  %.*s\n", SLICE_FMT(curr->name));
      }

   } else if (mode == MODE_FULL) {
      for (const content *c = start.content; c <= end.content; c++) {
         int x = printf("# %.*s/\n", SLICE_FMT(obj->fullname));
         int y = printf("# %.*s\n", SLICE_FMT(c->name));
         print_separator(MAX2(x, y));
         printf("\n");

         printf("%.*s\n", SLICE_FMT(c->data));
      }

   } else {
      for (const content *c = start.content; c < end.content; c++) {
         const content *next = c + 1;

         int x = printf("# %.*s/\n", SLICE_FMT(obj->fullname));
         int y = printf("# %.*s -> %.*s\n", SLICE_FMT(c->name), SLICE_FMT(next->name));
         print_separator(MAX2(x, y));
         printf("\n");

         diff(ctx, c->data, next->data);
         printf("\n");
      }
   }

   printf("\n");
   return 0;
}

static slice
get_spirv_disassembly(void *mem_ctx, object *obj)
{
   assert(slice_equal_cstr(obj->name, "SPV"));
   assert(obj->versions_count == 1);

   content *c = &obj->versions[0];

   int stdin_pipe[2], stdout_pipe[2];
   if (pipe(stdin_pipe) < 0 || pipe(stdout_pipe) < 0)
      return (slice){};

   pid_t pid = fork();
   if (pid < 0) {
      close(stdin_pipe[0]);
      close(stdin_pipe[1]);
      close(stdout_pipe[0]);
      close(stdout_pipe[1]);
      return (slice){};
   }

   /* Child process. */
   if (pid == 0) {
      close(stdin_pipe[1]);
      close(stdout_pipe[0]);

      dup2(stdin_pipe[0], STDIN_FILENO);
      dup2(stdout_pipe[1], STDOUT_FILENO);

      close(stdin_pipe[0]);
      close(stdout_pipe[1]);

      execvp("spirv-dis", (char *[]){"spirv-dis", "--color", "-", NULL});

      /* If exec fails, exit with error. */
      exit(1);
   }

   close(stdin_pipe[0]);
   close(stdout_pipe[1]);

   ssize_t written = write(stdin_pipe[1], c->data.data, c->data.len);
   close(stdin_pipe[1]);

   struct util_dynarray output;
   util_dynarray_init(&output, mem_ctx);

   if (written != (ssize_t)c->data.len)
      goto wait_and_fail;

   char read_buffer[1024];
   ssize_t bytes_read;
   while ((bytes_read = read(stdout_pipe[0], read_buffer, sizeof(read_buffer))) > 0) {
      if (!util_dynarray_grow_bytes(&output, bytes_read, 1))
         goto wait_and_fail;

      memcpy((char *)output.data + output.size - bytes_read, read_buffer, bytes_read);
   }

   close(stdout_pipe[0]);

   int status;
   waitpid(pid, &status, 0);

   if (WEXITSTATUS(status) != 0 || output.size == 0)
      goto fail;

   util_dynarray_append_typed(&output, char, '\0');

   return slice_from_cstr(output.data);

wait_and_fail:
   close(stdout_pipe[0]);
   waitpid(pid, NULL, 0);

fail:
   failf("mda: error when running spirv-dis");
   return (slice){};
}

static int
print_disassembled_spirv(void *mem_ctx, object *obj)
{
   slice disassembly = get_spirv_disassembly(mem_ctx, obj);
   if (slice_is_empty(disassembly)) {
      fprintf(stderr, "mda: failed to disassemble SPIR-V\n");
      return 1;
   }

   printf("%.*s\n", SLICE_FMT(disassembly));
   return 0;
}

static int
cmd_print(context *ctx)
{
   const bool raw = !strcmp(ctx->cmd_name, "printraw");

   if (ctx->args_count == 0) {
      fprintf(stderr, "mda: need to pass an object to print\n");
      return 1;
   }

   const char *pattern = ctx->args[0];

   match m = find_one(ctx, pattern);
   if (!m.object)
      return 1;

   if (!m.content)
      m.content = last_version(m.object);

   if (!raw) {
      if (slice_equal_cstr(m.object->name, "SPV"))
         return print_disassembled_spirv(ctx, m.object);

      int x = printf("### %.*s\n", SLICE_FMT(m.content->fullname));
      print_separator(x);
      printf("\n");
   }

   printf("%.*s", SLICE_FMT(m.content->data));

   if (!raw)
      printf("\n");

   return 0;
}

static int
print_search_matches(slice content, slice search_string, slice fullname)
{
#define CONTEXT_SIZE 2
   int match_count = 0;

   /* Keep track of previous non-matching lines in case a matching line
    * is found, so that context can be printed.
    */
   slice prev_lines[CONTEXT_SIZE];
   int unprinted_prev_lines = 0;

   /* Allow to "merge" multiple matches that are near to each other
    * in a single block of output.
    */
   int lines_since_match = -1;

   slice remaining = content;
   int line_num = 1;

   while (!slice_is_empty(remaining)) {
      slice_cut_result cut = slice_cut(remaining, '\n');
      slice line = cut.found ? cut.before : remaining;

      if (slice_contains_str(line, search_string)) {
         if (match_count == 0)
            printf("=== %.*s ===\n", SLICE_FMT(fullname));

         for (int i = 0; i < unprinted_prev_lines; i++) {
            int prev_line_num = line_num - unprinted_prev_lines + i;
            printf("%5d: %.*s\n", prev_line_num, SLICE_FMT(prev_lines[i]));
         }
         unprinted_prev_lines = 0;

         printf("%5d: %.*s\n", line_num, SLICE_FMT(line));

         match_count++;
         lines_since_match = 0;

      } else {
         /* Print context after a match. */
         if (lines_since_match >= 0) {
            if (lines_since_match < CONTEXT_SIZE) {
               printf("%5d: %.*s\n", line_num, SLICE_FMT(line));
               lines_since_match++;
            } else {
               printf("\n");
               lines_since_match = -1;
            }
         }

         /* Maintain the sliding window of previous lines only
          * if haven't printed them right above.
          */
         if (lines_since_match < 0) {
            if (unprinted_prev_lines < CONTEXT_SIZE) {
               prev_lines[unprinted_prev_lines++] = line;
            } else {
               /* Shift. */
               for (int i = 0; i < CONTEXT_SIZE - 1; i++)
                  prev_lines[i] = prev_lines[i + 1];
               prev_lines[CONTEXT_SIZE - 1] = line;
            }
         }
      }

      line_num++;
      remaining = cut.after;
   }

   if (match_count > 0)
      printf("\n");

   return match_count;
}

static int
cmd_search(context *ctx)
{
   bool search_all = !strcmp(ctx->cmd_name, "searchall");

   if (ctx->args_count < 1 || ctx->args_count > 2) {
      fprintf(stderr, "mda: %s requires 1-2 arguments\n", ctx->cmd_name);
      return 1;
   }

   slice search_string = slice_from_cstr(ctx->args[0]);
   const char *pattern = ctx->args_count > 1 ? ctx->args[1] : "";

   find_all_result matches = find_all(ctx, pattern);
   int found_count = 0;

   for (int i = 0; i < matches.matches_count; i++) {
      match *m = &matches.matches[i];

      /* SPIR-V object has only one version.  We probably could clean up
       * handling of it here and elsewhere to something more general
       * if we ever get another "special" object.
       */
      const bool is_spirv = slice_equal_cstr(m->object->name, "SPV");

      if (search_all && !is_spirv) {
         foreach_version(c, m->object)
            found_count += print_search_matches(c->data, search_string, c->fullname);

      } else {
         content *latest = last_version(m->object);

         slice search_data;
         if (is_spirv)
            search_data = get_spirv_disassembly(m->object->ma, m->object);
         else
            search_data = latest->data;

         found_count += print_search_matches(search_data, search_string, latest->fullname);
      }
   }

   if (found_count == 0)
      printf("No matches found\n");
   else
      printf("Found %d match%s\n", found_count, found_count == 1 ? "" : "es");

   return 0;
}

static void
open_manual()
{
   FILE *f = NULL;

   /* This fd will be set as stdin for executing man. */
   int fd = memfd_create("mda.1", 0);
   if (fd != -1)
      f = fdopen(fd, "w");

   if (!f) {
      /* Fallback to just printing the content out. */
      f = stderr;
   }

   static const char *contents[] = {
      ".TH mda 1 2025-03-29",
      "",
      ".SH NAME",
      "",
      "mda - reads mesa debugging archive files",
      "",
      ".SH SYNOPSIS",
      "",
      "mda [[-f PATH]... [-U[nnn]] [-Y[nnn]]] COMMAND [args]",
      "",
      ".SH DESCRIPTION",
      "",
      "Reads *.mda.tar files generated by Mesa drivers, these",
      "files contain debugging information about a pipeline or",
      "a single shader stage.",
      "",
      "Without command, all the objects are listed, an object can",
      "be a particular internal shader form or other metadata.",
      "Objects are identified by fuzzy matching a PATTERN with their",
      "names.  Names can be seen in 'list' commands.",
      "",
      "Objects may have multiple versions, e.g. multiple steps",
      "of a shader generated during optimization.  When not",
      "specified in the PATTERN, commands pick a relevant version,",
      "either first or last).",
      "",
      "By default all *.mda.tar files in the current directory are read.",
      "To specify which files or directories to read use one or more `-f PATH`",
      "flags before the command.  When PATH is a directory, all *.mda.tar files",
      "in that directory (non-recursively) are loaded.",
      "",
      ".SH COMMANDS",
      "",
      "    list                           list objects",
      "",
      "    listall                        list all versions of objects",
      "",
      "    listraw                        list all versions of objects with full names",
      "",
      "    print       PATTERN            formatted print an object",
      "",
      "    printraw    PATTERN            unformatted print an object",
      "",
      "    log         PATTERN [PATTERN]  print changes between versions of an object",
      "",
      "    logfull     PATTERN [PATTERN]  print full contents of versions of an object",
      "",
      "    log1        PATTERN [PATTERN]  print names of the versions of an object",
      "",
      "    diff        PATTERN PATTERN    compare two objects",
      "",
      "    search      STRING [PATTERN]   search latest versions for string",
      "",
      "    searchall   STRING [PATTERN]   search all versions for string",
      "",
      "    info                           print metadata about the archive",
      "",
      ".SH OPTIONS",
      "",
      "    -f PATH                        read a file, or mda.tar files in a directory",
      "                                   non-recursively.  Multiple -f can be used.",
      "                                   If no -f provided, current directory is used.",
      "",
      "    -U[nnn]                        use unified diff (default: 5 context lines)",
      "",
      "    -Y[nnn]                        use side-by-side diff (default: 240 width)",
      "",
      "The -U and -Y options are mutually exclusive. If neither is specified,",
      "-U5 is used by default.",
      "",
      ".SH ENVIRONMENT VARIABLES",
      "",
      "The diff program used by mda can be configured by setting",
      "the MDA_DIFF_COMMAND environment variable, which overrides",
      "the -U and -Y options. Without MDA_DIFF_COMMAND:",
      "",
      "    -U uses: git diff --no-index --color-words -Unnn -- %s %s | tail -n +5",
      "    -Y uses: diff -y -Wnnn %s %s",
      "",
      "When showing SPIR-V files, spirv-dis tool is used.",
      ""
   };

   for (int i = 0; i < ARRAY_SIZE(contents); i++) {
      fputs(contents[i], f);
      putc('\n', f);
   }

   fflush(f);

   if (f != stderr) {
      /* Inject the temporary as stdin for man. */
      lseek(fd, 0, SEEK_SET);
      dup2(fd, STDIN_FILENO);
      fclose(f);

      execlp("man", "man", "-l", "-", (char *)NULL);
   } else {
      exit(0);
   }
}

static void
print_help()
{
   printf("mda [[-f PATH]... [-U[nnn]] [-Y[nnn]]] CMD [ARGS...]\n"
          "\n"
          "OPTIONS\n"
          "\n"
          "    -f PATH                        read a file, or mda.tar files in a directory\n"
          "                                   non-recursively.  Multiple -f can be used.\n"
          "                                   If no -f provided, current directory is used.\n"
          "    -U[nnn]                        use unified diff (default: 5 context lines)\n"
          "    -Y[nnn]                        use side-by-side diff (default: 240 width)\n"
          "\n"
          "COMMANDS\n"
          "\n"
          "    list                           list objects\n"
          "    listall                        list all versions of objects\n"
          "    listraw                        list all versions of objects with full names\n"
          "    print       PATTERN            formatted print an object\n"
          "    printraw    PATTERN            unformatted print an object\n"
          "    log         PATTERN [PATTERN]  print changes between versions of an object\n"
          "    logfull     PATTERN [PATTERN]  print full contents of versions of an object\n"
          "    log1        PATTERN [PATTERN]  print names of the versions of an object\n"
          "    diff        PATTERN PATTERN    compare two objects\n"
          "    search      STRING [PATTERN]   search latest versions for string\n"
          "    searchall   STRING [PATTERN]   search all versions for string\n"
          "    info                           print metadata about the archive\n"
          "\n"
          "ENVIRONMENT VARIABLES\n"
          "\n"
          "    MDA_DIFF_COMMAND               custom diff command (overrides -U/-Y)\n"
          "\n"
          "Default diff mode is -U5 (unified diff with 5 context lines).\n"
          "For more details, use 'mda help' to open the manual.\n");
}

static bool
load_archive(context *ctx, const char *filename)
{
   struct mesa_archive *ma = parse_mesa_archive(ctx, filename);
   if (!ma)
      return false;

   ctx->archives = rerzalloc(ctx, ctx->archives, mesa_archive *, ctx->archives_count,
                             ctx->archives_count + 1);
   ctx->archives[ctx->archives_count] = ma;
   ctx->archives_count++;
   return true;
}

static void
load_archives_from_directory(context *ctx, const char *dirpath)
{
   DIR *d = opendir(dirpath);
   if (!d) {
      fprintf(stderr, "mda: couldn't open directory %s: %s\n", dirpath, strerror(errno));
      return;
   }

   struct dirent *dir;
   while ((dir = readdir(d)) != NULL) {
      slice filename = slice_from_cstr(dir->d_name);
      slice mda_ext = slice_from_cstr(".mda.tar");
      if (slice_ends_with(filename, mda_ext)) {
         char *fullpath = ralloc_asprintf(ctx, "%s/%s", dirpath, dir->d_name);
         if (!load_archive(ctx, fullpath)) {
            /* Failed to parse. */
            fprintf(stderr, "mda: ignoring file after parsing failure: %s\n", fullpath);
         }
         ralloc_free(fullpath);
      }
   }
   closedir(d);
}

static pid_t
setup_pager()
{
   if (!isatty(STDOUT_FILENO) ||
       os_get_option("NO_PAGER"))
      return 0;

   const char *term = os_get_option("TERM");
   if (!term || !strcmp(term, "dumb"))
      return 0;

   int pipefd[2];
   if (pipe(pipefd) == -1) {
      fprintf(stderr, "mda: couldn't create pipe for pager\n");
      return 0;
   }

   pid_t pid = fork();
   if (pid == -1) {
      close(pipefd[0]);
      close(pipefd[1]);
      fprintf(stderr, "mda: couldn't open pager\n");
      return 0;
   }

   if (pid == 0) {
      /* Child stdin will read from pipe. */
      close(pipefd[1]);
      dup2(pipefd[0], STDIN_FILENO);
      close(pipefd[0]);

      const char *pager = os_get_option("PAGER");
      if (pager && *pager)
         execlp(pager, pager, NULL);

      execlp("less", "less", "-FSRi", NULL);
      execlp("more", "more", NULL);
      execlp("cat", "cat", NULL);
      exit(1);
   }

   /* Parent stdout will point to pipe. */
   close(pipefd[0]);
   dup2(pipefd[1], STDOUT_FILENO);
   close(pipefd[1]);

   return pid;
}

int
main(int argc, char *argv[])
{
   if (argc >= 2) {
      if (!strcmp(argv[1], "help") ||
          !strcmp(argv[1], "--help")) {
         open_manual();
         return 0;
      } else if (!strcmp(argv[1], "-h")) {
         print_help();
         return 0;
      }
   }

   context *ctx = rzalloc(NULL, context);
   ctx->diff.mode = DIFF_UNIFIED;
   ctx->diff.param = 5;

   bool diff_set = false;
   int cur_arg = 1;

   /* Set to deduplicate -f arguments. */
   struct hash_table *seen_paths = _mesa_string_hash_table_create(NULL);

   while (cur_arg < argc && argv[cur_arg][0] == '-') {
      if (!strcmp(argv[cur_arg], "-f")) {
         if (argc == cur_arg + 1)
            failf("mda: missing filename after -f flag\n");

         const char *path = argv[cur_arg + 1];
         cur_arg += 2;

         /* Skip if we've already seen this path. */
         if (_mesa_hash_table_search(seen_paths, path))
            continue;

         _mesa_hash_table_insert(seen_paths, path, NULL);

         struct stat st;
         if (stat(path, &st) != 0)
            failf("mda: couldn't access path %s: %s\n", path, strerror(errno));

         if (S_ISDIR(st.st_mode)) {
            load_archives_from_directory(ctx, path);
         } else if (S_ISREG(st.st_mode)) {
            if (!load_archive(ctx, path))
               failf("mda: failed to parse file: %s\n", path);
         } else {
            failf("mda: path is not a file or directory: %s\n", path);
         }
      } else if (argv[cur_arg][1] == 'U' || argv[cur_arg][1] == 'Y') {
         if (diff_set)
            failf("mda: -U and -Y options are mutually exclusive\n");

         diff_set = true;
         ctx->diff.mode = (argv[cur_arg][1] == 'U') ? DIFF_UNIFIED : DIFF_SIDE_BY_SIDE;

         /* Parse optional numeric parameter. */
         if (argv[cur_arg][2] != '\0')
            ctx->diff.param = atoi(&argv[cur_arg][2]);
         else
            ctx->diff.param = ctx->diff.mode == DIFF_UNIFIED ? 5 : 240;

         cur_arg++;
      } else {
         /* Unknown flag, stop parsing flags */
         break;
      }
   }

   _mesa_hash_table_destroy(seen_paths, NULL);

   if (ctx->archives_count == 0) {
      /* Load all mda files in the current directory. */
      load_archives_from_directory(ctx, ".");

      if (ctx->archives_count == 0)
         failf("Couldn't load any *.mda.tar files in the current directory\n");
   }

   ctx->cmd_name = cur_arg < argc ? argv[cur_arg++] : "list";
   ctx->args_count = argc - cur_arg;
   ctx->args = rzalloc_array(ctx, char *, argc - cur_arg + 1);
   for (int i = 0; i < ctx->args_count; i++)
      ctx->args[i] = ralloc_strdup(ctx, argv[cur_arg + i]);

   struct command {
      const char *name;
      int (*func)(context *ctx);
      bool skip_pager;
   };

   static const struct command cmds[] = {
      { "diff",       cmd_diff },
      { "info",       cmd_info, .skip_pager = true },
      { "list",       cmd_list },
      { "listall",    cmd_list },
      { "listraw",    cmd_listraw },
      { "log",        cmd_log },
      { "log1",       cmd_log },
      { "logfull",    cmd_log },
      { "print",      cmd_print },
      { "printraw",   cmd_print, .skip_pager = true },
      { "search",     cmd_search },
      { "searchall",  cmd_search },
   };

   const struct command *cmd = NULL;
   for (const struct command *c = cmds; c < cmds + ARRAY_SIZE(cmds); c++) {
      if (!strcmp(c->name, ctx->cmd_name)) {
         cmd = c;
         break;
      }
   }

   if (!cmd) {
      fprintf(stderr, "mda: unknown command '%s'\n", ctx->cmd_name);
      print_help();
      return 1;
   }

   pid_t pid = cmd->skip_pager ? -1 : setup_pager();

   int r = cmd->func(ctx);
   ralloc_free(ctx);

   if (pid > 0) {
      fflush(stdout);
      fclose(stdout);
      waitpid(pid, NULL, 0);
   }

   return r;
}
