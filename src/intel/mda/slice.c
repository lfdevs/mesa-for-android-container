/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include "util/ralloc.h"
#include "util/hash_table.h"

#include "slice.h"

slice
slice_from_cstr(const char *str)
{
   return (slice){ str, str ? strlen(str) : 0 };
}

bool
slice_is_empty(slice s)
{
   return s.len == 0;
}

bool
slice_equal(slice a, slice b)
{
   return a.len == b.len && (a.len == 0 || memcmp(a.data, b.data, a.len) == 0);
}

bool
slice_equal_cstr(slice s, const char *cstr)
{
   slice cstr_slice = slice_from_cstr(cstr);
   return slice_equal(s, cstr_slice);
}

char *
slice_to_cstr(void *mem_ctx, slice s)
{
   char *str = ralloc_size(mem_ctx, s.len + 1);
   memcpy(str, s.data, s.len);
   str[s.len] = '\0';
   return str;
}

slice
slice_find_char(slice s, char c)
{
   for (int i = 0; i < s.len; i++) {
      if (s.data[i] == c) {
         return slice_substr_from(s, i);
      }
   }
   return (slice){0};
}

slice
slice_find_str(slice s, slice needle)
{
   if (needle.len == 0)
      return s;
   if (needle.len > s.len)
      return (slice){0};

   for (int i = 0; i <= s.len - needle.len; i++) {
      if (memcmp(s.data + i, needle.data, needle.len) == 0) {
         return slice_substr_from(s, i);
      }
   }
   return (slice){0};
}

bool
slice_contains_str(slice s, slice needle)
{
   return !slice_is_empty(slice_find_str(s, needle));
}

bool
slice_starts_with(slice s, slice prefix)
{
   if (prefix.len > s.len)
      return false;
   return memcmp(s.data, prefix.data, prefix.len) == 0;
}

bool
slice_ends_with(slice s, slice suffix)
{
   if (suffix.len > s.len)
      return false;
   return memcmp(s.data + s.len - suffix.len, suffix.data, suffix.len) == 0;
}

slice
slice_strip_prefix(slice s, slice prefix)
{
   if (slice_starts_with(s, prefix))
      return slice_substr_from(s, prefix.len);
   return s;
}

slice
slice_substr_from(slice s, int start)
{
   if (start < 0)
      start = 0;
   if (start >= s.len)
      return (slice){0};
   return (slice){.data = s.data + start, .len = s.len - start};
}

slice
slice_substr_to(slice s, int end)
{
   if (end < 0)
      end = 0;
   if (end > s.len)
      end = s.len;
   return (slice){.data = s.data, .len = end};
}

slice
slice_substr(slice s, int start, int end)
{
   if (start < 0)
      start = 0;
   if (end > s.len)
      end = s.len;

   if (start >= end)
      return (slice){0};
   return (slice){.data = s.data + start, .len = end - start};
}

slice_cut_result
slice_cut(slice s, char c)
{
   return slice_cut_n(s, c, 1);
}

slice_cut_result
slice_cut_n(slice s, char c, int n)
{
   if (n <= 0) {
      slice_cut_result result = { .before = s, .after = {0}, .found = false };
      return result;
   }

   slice current = s;
   int count = 0;

   while (!slice_is_empty(current)) {
      slice found = slice_find_char(current, c);
      if (slice_is_empty(found))
         break;

      count++;
      if (count == n) {
         int pos = found.data - s.data;
         slice_cut_result result = {
            .before = slice_substr_to(s, pos),
            .after = slice_substr_from(s, pos + 1),
            .found = true
         };
         return result;
      }

      /* Move past this occurrence. */
      current = slice_substr_from(found, 1);
   }

   /* Not enough occurrences found. */
   slice_cut_result result = { .before = s, .after = {0}, .found = false };
   return result;
}

static uint32_t
slice_ptr_hash(const void *key)
{
   const slice *s = (const slice *)key;
   return _mesa_hash_string_with_length(s->data, s->len);
}

static bool
slice_ptr_equal(const void *a, const void *b)
{
   const slice *sa = (const slice *)a;
   const slice *sb = (const slice *)b;
   return slice_equal(*sa, *sb);
}

struct hash_table *
slice_hash_table_create(void *mem_ctx)
{
   return _mesa_hash_table_create(mem_ctx, slice_ptr_hash, slice_ptr_equal);
}

struct hash_entry *
slice_hash_table_insert(struct hash_table *ht, slice key, void *data)
{
   /* Create a new slice that will be stable in memory, so its pointer can
    * be used as key.
    */
   slice *key_copy = rzalloc(ht, slice);
   *key_copy = key;

   return _mesa_hash_table_insert(ht, key_copy, data);
}
